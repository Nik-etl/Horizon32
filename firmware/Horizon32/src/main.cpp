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
//  DRAWING LOGIC 
//-----------------------------
void draw_to_ram(const SensorData& data) {
  u8g2.clearBuffer(); // Clear the ESP32 RAM buffer

  u8g2.setFont(u8g2_font_jinxedwizards_tr);
  
  // Accelerometer
  u8g2.drawStr(5, 0, "X:");  
  u8g2.setCursor(20, 0); u8g2.print(data.a.acceleration.x);
  u8g2.drawStr(5, 20, "Y:");  
  u8g2.setCursor(20, 20); u8g2.print(data.a.acceleration.y);
  u8g2.drawStr(5, 40, "Z:");  
  u8g2.setCursor(20, 40); u8g2.print(data.a.acceleration.z);

  // Gyroscope
  u8g2.drawStr(60, 0, "X:");  
  u8g2.setCursor(80, 0); u8g2.print(data.g.gyro.x);
  u8g2.drawStr(60, 20, "Y:");  
  u8g2.setCursor(80, 20); u8g2.print(data.g.gyro.y);
  u8g2.drawStr(60, 40, "Z:");  
  u8g2.setCursor(80, 40); u8g2.print(data.g.gyro.z);
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

  // Wait for the FIRST piece of data to arrive
  xQueueReceive(sensorMailbox, &displayData, portMAX_DELAY);
  // Draw and send the FULL screen once
  draw_to_ram(displayData);
  if (xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
    u8g2.sendBuffer();
    xSemaphoreGive(i2cMutex);
  }
  for (;;) {
    
    if (xQueueReceive(sensorMailbox, &displayData, pdMS_TO_TICKS(100)) == pdTRUE) {
      
      // 2. Draw everything into ESP32 RAM (Fast, CPU only, no Mutex)
      draw_to_ram(displayData);

      // 3. Transmit to OLED safely over I2C in chunks
        // --- CHUNK 1: Accelerometer ---
      if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        u8g2.updateDisplayArea(2, 0, 6, 6);
        xSemaphoreGive(i2cMutex);
      }
      
      // YIELD: Give the Sensor Task a 2ms window to grab the Mutex 
      // in between sending display chunks!
      vTaskDelay(pdMS_TO_TICKS(2));

      // --- CHUNK 2: Gyroscope ---
      if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        u8g2.updateDisplayArea(10, 0, 6, 6);
        xSemaphoreGive(i2cMutex);
      }
    }
  }
    
    // Sleep roughly 40ms to hit a ~20Hz (50ms) refresh rate target
    vTaskDelay(pdMS_TO_TICKS(40)); 
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