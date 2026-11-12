#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <U8g2lib.h>
#include <Wire.h>

/*
TODO 
Optimize display area - at least to bring write time down NOT NEEDED MPU falls fine in deadline 20ms, can be called in between each section write 

NOT IN ORDER 
- Graphics
- RTOS task planning 
- FreeRTOS integration
- Incorporate filter IIR or FIR to smooth accelerometer
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
struct SensorData {
  sensors_event_t a;
  sensors_event_t g;
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
void draw_to_ram(const SensorData& data) {
  u8g2.clearBuffer(); 
  u8g2.setDrawColor(1); // 1 = White. Force visible pixels

  // 1. Calculate rough Pitch and Roll from Accelerometer (in Radians)
  float roll = atan2(data.a.acceleration.y, data.a.acceleration.z);
  float pitch = atan2(-data.a.acceleration.x, 
                sqrt(data.a.acceleration.y * data.a.acceleration.y + 
                     data.a.acceleration.z * data.a.acceleration.z));

  // 2. Map Pitch to a vertical pixel shift (Center is Y=32)
  int y_center = 32 + (int)(pitch * 30.0);

  // 3. Calculate the endpoints of the horizon line based on Roll
  int x1 = 64 - (int)(cos(roll) * 40);
  int y1 = y_center - (int)(sin(roll) * 40);
  int x2 = 64 + (int)(cos(roll) * 40);
  int y2 = y_center + (int)(sin(roll) * 40);

  // 4. Draw the moving horizon line
  u8g2.drawLine(x1, y1, x2, y2);

  // 5. Draw a static airplane crosshair in the exact center (64, 32)
  u8g2.drawPixel(64, 32);       // Center dot
  u8g2.drawHLine(54, 32, 8);    // Left wing
  u8g2.drawHLine(66, 32, 8);    // Right wing
  u8g2.drawVLine(64, 24, 6);    // Tail
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
  sensorMailbox = xQueueCreate(1, sizeof(SensorData)); // Length 1 for latest data only
  commandQueue = xQueueCreate(5, sizeof(SystemCommand)); // Queue for button presses

  // 3. Launch Tasks
  if (i2cMutex != NULL && sensorMailbox != NULL && commandQueue != NULL) {
    // xTaskCreate(Function, Name, Stack Size, Params, Priority, Handle)
    // Priority: 3 is highest, 1 is lowest
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
  SensorData currentData;
  sensors_event_t temp; // Required by getEvent but unused
  SystemCommand cmd;

  // 1. Initialize the tick count variable
  TickType_t xLastWakeTime;
  xLastWakeTime = xTaskGetTickCount();

  for (;;) {

    // 1. Check for incoming button commands (Non-blocking)
    if (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {
      if (cmd == CMD_CALIBRATE) {
        Serial.println("Calibration triggered! (Placeholder to collect N samples)");
        // TODO: Collect samples and average bias here
      }
    }

    // 2. Safely claim I2C Bus and read IMU
    // We wait up to 5ms for the Mutex. If the display has it, we wait.
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      mpu.getEvent(&currentData.a, &currentData.g, &temp);
      xSemaphoreGive(i2cMutex); // Release bus instantly
    } else {
      Serial.println("WARNING: Sensor Task missed I2C deadline!");
    }

    // 3. TODO: Run Attitude Filter math here

    // 4. Overwrite mailbox with the freshest data for the Display Task
    xQueueOverwrite(sensorMailbox, &currentData);

    // 5. Sleep exactly enough to maintain 100Hz (10ms deadline)
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
  }
}

//-----------------------------
//    DISPLAY TASK
//-----------------------------

void displayTask(void *pvParameters) {
  SensorData displayData;
  Serial.println("Display Task started.");

  for (;;) {
    // 1. Wait for data, but don't wait forever. Wait 1 second max.
    if (xQueueReceive(sensorMailbox, &displayData, pdMS_TO_TICKS(1000)) == pdTRUE) {
      
      draw_to_ram(displayData);

      // 2. Temporarily use a full sendBuffer() to prove the OLED works
      if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        u8g2.sendBuffer();
        xSemaphoreGive(i2cMutex);
      } else {
        Serial.println("Display Task: Couldn't get I2C Mutex!");
      }
      
    } else {
      // If we see this in the Serial Monitor, the Sensor Task is broken/frozen!
      Serial.println("Display Task: Mailbox is empty! Sensor Task is not sending data.");
    }
    
    // Sleep to maintain ~20Hz
    vTaskDelay(pdMS_TO_TICKS(50)); 
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
