#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <U8g2lib.h>
#include <Wire.h>

/*
TODO 
Optimize display area - at least to bring write time down NOT NEEDED MPU falls fine in deadline 20ms, can be called in between each section write 

NOT IN ORDER 
- Benchmark draw function, sensor math, etc. 
- Gyro implementation
- Graphics
- FreeRTOS integration
- Incorporate filter IIR or FIR to smooth accelerometer
- LAST: background graphics ... horizon
*/
// --- HARDWARE & CONFIGS ---
Adafruit_MPU6050 mpu;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(/* rotation=*/U8G2_R0, /* reset=*/ U8X8_PIN_NONE); 

constexpr uint8_t SDA_PIN = 8; //blue
constexpr uint8_t SCL_PIN = 9; //yellow
constexpr uint8_t BUTTON_PIN = 42;
constexpr unsigned long I2C_FREQUENCY = 400000; //I2C configured to 400KHz (Fast mode)

// --- FreeRTOS Handles ---
SemaphoreHandle_t i2cMutex;
QueueHandle_t sensorMailbox;
QueueHandle_t commandQueue;

// --- Data Structures ---
struct AttitudeData {
  float pitch; //  rads (for now)
  float roll;  //  rads (for now)
};

enum SystemCommand { CMD_NONE, CMD_CALIBRATE };

bool firstFrame = true; // send full buffer flag

// --- Task Prototypes ---
void sensorTask(void *pvParameters);
void displayTask(void *pvParameters);
void buttonTask(void *pvParameters);

// filter stuff
// const int NUM_SAMPLES = 10; 
// const int NUM_AXES = 3;

// constexpr uint8_t MPU_address = 0x68;
// constexpr uint8_t OLED_address = 0x3c;

//-----------------------------
//  DRAWING LOGIC (Graphics Mode)
//-----------------------------
// TODO: implement edge handling
void draw_to_ram(const AttitudeData& attitude) {
  u8g2.clearBuffer(); 
  u8g2.setDrawColor(1); 

  // 1. Map Pitch to a vertical pixel shift (Center is Y=32)
  int y_center = 32 + (int)(attitude.pitch * 30.0);

  // 2. Calculate the endpoints of the horizon line based on Roll
  int x1 = 64 - (int)(cos(attitude.roll) * 40);
  int y1 = y_center - (int)(sin(attitude.roll) * 40);
  int x2 = 64 + (int)(cos(attitude.roll) * 40);
  int y2 = y_center + (int)(sin(attitude.roll) * 40);

  // 3. Draw the moving horizon line
  u8g2.drawLine(x1, y1, x2, y2);

  // 4. Draw a static airplane crosshair in the exact center (64, 32)
  u8g2.drawPixel(64, 32);       
  u8g2.drawHLine(54, 32, 8);    
  u8g2.drawHLine(66, 32, 8);    
  u8g2.drawVLine(64, 24, 6);    
}

//-----------------------------
//  SYSTEM SETUP
//-----------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  
  // 1. Init I2C Hardware
  Wire.begin(SDA_PIN, SCL_PIN); 

  if (!mpu.begin()) {
    Serial.println("MPU-6050 not found — run the I2C scanner first.");
    while (true) delay(1000);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G); 
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);      
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);   

  u8g2.setBusClock(I2C_FREQUENCY);
  u8g2.begin();       
  u8g2.setFontPosTop(); 

  // 2. Initialize RTOS Primitives
  i2cMutex = xSemaphoreCreateMutex();
  sensorMailbox = xQueueCreate(1, sizeof(AttitudeData)); // Length 1 for latest data only
  commandQueue = xQueueCreate(5, sizeof(SystemCommand)); // Queue for button presses

  // 3. Launch Tasks
  if (i2cMutex != NULL && sensorMailbox != NULL && commandQueue != NULL) {
    // xTaskCreate(Function, Name, Stack Size, Params, Priority, Handle)
    // priority: 3 highest, 1 lowest
    xTaskCreatePinnedToCore(sensorTask,  "Sensor",  4096, NULL, 3, NULL, 1); 
    xTaskCreatePinnedToCore(buttonTask,  "Button",  2048, NULL, 2, NULL, 1); 
    xTaskCreatePinnedToCore(displayTask, "Display", 4096, NULL, 1, NULL, 1); 
  }
}

  void loop() {
  vTaskDelete(NULL); // deletes the default Arduino loop task to save RAM
}

//-----------------------------
//    SENSOR TASK
//-----------------------------

void sensorTask(void *pvParameters) {
  AttitudeData currentAttitude; // New struct
  sensors_event_t a, g, temp; 
  SystemCommand cmd;

  TickType_t xLastWakeTime = xTaskGetTickCount(); 

  for (;;) {
    if (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {
      if (cmd == CMD_CALIBRATE) Serial.println("Calibration triggered!");
    }

    // 1. Read the raw data
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      mpu.getEvent(&a, &g, &temp);
      xSemaphoreGive(i2cMutex); 
    }

    // 2. THE ATTITUDE FILTER (Physics Math)
    // For now, it's just accelerometer math
    // TODO: GYRO MATH 
    currentAttitude.roll = atan2(a.acceleration.y, a.acceleration.z);
    currentAttitude.pitch = atan2(-a.acceleration.x, 
                            sqrt(a.acceleration.y * a.acceleration.y + 
                                 a.acceleration.z * a.acceleration.z));

    // 3. Send the computed angles to the display
    xQueueOverwrite(sensorMailbox, &currentAttitude);
    
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10)); // 100Hz
  }
}

//-----------------------------
//    DISPLAY TASK
//-----------------------------

void displayTask(void *pvParameters) {
  AttitudeData displayData;
  Serial.println("Display Task started.");

  xQueueReceive(sensorMailbox, &displayData, portMAX_DELAY);
  
  // Draw it to the ESP32 RAM
  draw_to_ram(displayData);
  
  // Force a full screen update to wipe out any old text/garbage on the OLED
  if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    u8g2.sendBuffer();
    xSemaphoreGive(i2cMutex);
  }

  // ==========================================
  // 2. MAIN LOOP (Optimized Chunking)
  // ==========================================
  for (;;) {
    // Wait up to 100ms for fresh data
    if (xQueueReceive(sensorMailbox, &displayData, pdMS_TO_TICKS(100)) == pdTRUE) {
      
      // Calculate angles for wing orientation and draw all pixels to ESP32 RAM 
      draw_to_ram(displayData);

      // Transmit the screen over I2C in 4 horizontal slices
      // OLED has 8 rows 
      // currently sending 2 rows at a time
      for (uint8_t row = 0; row < 8; row += 2) {
        
        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          // updateDisplayArea(tile_x, tile_y, width_tiles, height_tiles)
          // starts at X=0, Y=page, full width (16), 2 tiles high
          u8g2.updateDisplayArea(0, row, 16, 2);
          xSemaphoreGive(i2cMutex);
        }
        
        // YIELD: Give the 100Hz Sensor Task a 2ms window to grab 
        // the I2C Mutex in between each visual slice we send!
        vTaskDelay(pdMS_TO_TICKS(2));
      }
      
    } else {
      Serial.println("Display Task: Mailbox is empty! Sensor Task is not sending data.");
    }
    
    // Sleep to maintain ~20Hz refresh rate (50ms total per frame)
    vTaskDelay(pdMS_TO_TICKS(30)); 
  }
}
//-----------------------------
//    BUTTON TASK
//-----------------------------

void buttonTask(void *pvParameters) {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  bool lastState = HIGH;
  uint32_t pressStartTime = 0;

  for (;;) {
    bool currentState = digitalRead(BUTTON_PIN);

    // Falling edge (Button just pressed down)
    if (currentState == LOW && lastState == HIGH) {
      pressStartTime = millis();
    }
    // Rising edge (Button released)
    else if (currentState == HIGH && lastState == LOW) {
      // Calculate how long it was held
      uint32_t holdDuration = millis() - pressStartTime;
      
      // If held for over 1 second, send Calibration command
      if (holdDuration >= 1000) {
        SystemCommand cmd = CMD_CALIBRATE;
        xQueueSend(commandQueue, &cmd, 0); // Send to command queue
      }
    }
    
    lastState = currentState;

    // Sleep 30ms (software debouncing)
    vTaskDelay(pdMS_TO_TICKS(30)); 
  }
}
