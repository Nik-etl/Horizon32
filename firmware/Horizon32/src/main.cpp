#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <U8g2lib.h>
#include <Wire.h>

/*
TODO 

NOT IN ORDER 
- Benchmark draw function, sensor math, etc. 
- Evaluate calibration task need..
- High pitch warning
- LAST: background graphics ... horizon edge handling: when line touches top 
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
SemaphoreHandle_t buttonSemaphore; 

// --- Data Structures ---
struct AttitudeData {
  float pitch; //  rads (for now)
  float roll;  //  rads (for now)
  bool isCalibrating; //could use enum SystemState 
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

  // calibration draw
  if (attitude.isCalibrating) {
    u8g2.setFont(u8g2_font_ncenB08_tr); // Standard readable font
    u8g2.drawStr(20, 30, "Calibrating...");
    return; // prevents further drawing 
  }

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
//   ISR
//-----------------------------

void ARDUINO_ISR_ATTR buttonISR() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  
  // Give the semaphore to wake up the Button Task
  xSemaphoreGiveFromISR(buttonSemaphore, &xHigherPriorityTaskWoken);
  
  // If waking the Button Task requires a context switch, do it immediately
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

//-----------------------------
//  SYSTEM SETUP
//-----------------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  
  // 1. Init I2C Hardware
  Wire.begin(SDA_PIN, SCL_PIN); 

    Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());

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
  buttonSemaphore = xSemaphoreCreateBinary();
  
  // Interrupt initialization
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, CHANGE);

  // 3. Launch Tasks
  if (i2cMutex != NULL && sensorMailbox != NULL && commandQueue != NULL) { //TODO: PDPASS CONDITIONAL
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
  AttitudeData currentAttitude; 
  currentAttitude.isCalibrating = false;

  // --- Filter & Calibration Variables ---
  float gyro_pitch = 0.0; 
  float gyro_roll = 0.0;  
  float pitch_bias = 0.0; 
  float roll_bias = 0.0;  
  const float dt = 0.01;    
  const float alpha = 0.98;

  sensors_event_t a, g, temp; 
  SystemCommand cmd;

  TickType_t xLastWakeTime = xTaskGetTickCount(); 

  for (;;) {
    if (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {
      if (cmd == CMD_CALIBRATE){
        // Step A: Tell the display we are starting calibration
        currentAttitude.isCalibrating = true;
        xQueueOverwrite(sensorMailbox, &currentAttitude);

        float sum_pitch = 0.0;
        float sum_roll = 0.0;
        const int num_samples = 100;

        // Step B: Take 100 readings (at 100hz = 1 second)
        for (int i = 0; i < num_samples;i++){
          if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
          mpu.getEvent(&a, &g, &temp);
          xSemaphoreGive(i2cMutex); 
          }
          
          float sample_roll = atan2(a.acceleration.y, a.acceleration.z);
          float sample_pitch = atan2(-a.acceleration.x, 
            sqrt(a.acceleration.y * a.acceleration.y + 
              a.acceleration.z * a.acceleration.z));
              
              sum_roll += sample_roll;
              sum_pitch += sample_pitch;

        vTaskDelay(pdMS_TO_TICKS(10)); // Wait 10ms before next sample
        }
        
        // Average biases calculation
        pitch_bias = sum_pitch / num_samples;
        roll_bias = sum_roll / num_samples;

        // Reset running filter angles
        gyro_pitch = 0.0;
        gyro_roll = 0.0;

        // Step C: Calibration done, resume normal operation
        currentAttitude.isCalibrating = false;
      }
    }

    // 1. Read the raw data
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      mpu.getEvent(&a, &g, &temp);
      xSemaphoreGive(i2cMutex); 
    }

    // 2. THE ATTITUDE FILTER (Physics Math)

    float accel_roll = atan2(a.acceleration.y, a.acceleration.z) - roll_bias;
    float accel_pitch = atan2(-a.acceleration.x, 
                        sqrt(a.acceleration.y * a.acceleration.y + 
                             a.acceleration.z * a.acceleration.z)) - pitch_bias;

    // Gyroscope (integration, time slice/dt = 0.01s)
    gyro_roll = gyro_roll + (g.gyro.x * dt);
    gyro_pitch = gyro_pitch + (g.gyro.y * dt);

    // Gyro makes up 98% of new angle 
    gyro_roll = (alpha * gyro_roll) + ((1.0 - alpha) * accel_roll);
    gyro_pitch = (alpha * gyro_pitch) + ((1.0 - alpha) * accel_pitch);

    currentAttitude.roll = gyro_roll;
    currentAttitude.pitch = gyro_pitch;

    // 3. Send the computed angles to the display
    xQueueOverwrite(sensorMailbox, &currentAttitude);
    // waits exactly 10ms to ensure it meets 100Hz deadline
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
  // 2. MAIN LOOP 
  // ==========================================
  for (;;) {
    // Wait up to 100ms for fresh data
    if (xQueueReceive(sensorMailbox, &displayData, pdMS_TO_TICKS(100)) == pdTRUE) {

      // Calibration override
      if (displayData.isCalibrating) {
        
        // 1. Draw the text to RAM
        draw_to_ram(displayData);
        
        // 2. Send it ONCE using a fast full-screen update
        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          u8g2.sendBuffer();
          xSemaphoreGive(i2cMutex);
        }
      // display task is blocked until new sensor data is sent after calibration
      xQueueReceive(sensorMailbox, &displayData, portMAX_DELAY);
      }

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
  uint32_t pressStartTime = 0;
  bool isTrackingPress = false;

  for (;;) {
    // 1. Sleep until ISR sends semaphore
    if (xSemaphoreTake(buttonSemaphore, portMAX_DELAY) == pdTRUE) {

      // 2. Wait 20ms to let the physical button spring stop bouncing (Debounce)
      vTaskDelay(pdMS_TO_TICKS(20));

      // 3. Read the stable state of the pin
      bool isPressed = (digitalRead(BUTTON_PIN) == LOW);

      // 4. Handle the logic
      if (isPressed && !isTrackingPress) {
        pressStartTime = millis();
        isTrackingPress = true;
      } 
      else if (!isPressed && isTrackingPress) {
        // Button was just released
        isTrackingPress = false;

        // Evaluate button press time
        if ((millis() - pressStartTime) >= 1000) {
          SystemCommand cmd = CMD_CALIBRATE;
          xQueueSend(commandQueue, &cmd, 0); 
        }
      }

      // Clear out any extra semaphores that piled up during the 20ms bounce
      xQueueReset(buttonSemaphore); 
    }
  }
}