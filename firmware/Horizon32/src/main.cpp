#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <U8g2lib.h>
#include <Wire.h>

/*
TODO 

NOT IN ORDER 
- Graphics
- RTOS task planning 
- FreeRTOS integration
- Incorporate filter IIR or FIR to smooth accelerometer
*/
Adafruit_MPU6050 mpu;

U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(/* rotation=*/U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

constexpr uint8_t SDA_PIN = 8; //blue
constexpr uint8_t SCL_PIN = 9; //yellow
constexpr unsigned long I2C_FREQUENCY = 400000; //I2C configured to 400KHz (Fast mode)

// filter stuff
// const int NUM_SAMPLES = 10; 
// const int NUM_AXES = 3;

// constexpr uint8_t MPU_address = 0x68;
// constexpr uint8_t OLED_address = 0x3c;

void draw(sensors_event_t a,sensors_event_t g)
{
      //accelerometer
      // u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_jinxedwizards_tr);
      u8g2.drawStr(5, 0, "X:");  
      u8g2.setCursor(20, 0);
      u8g2.print(a.acceleration.x);
      u8g2.drawStr(5, 20, "Y:");  
      u8g2.setCursor(20, 20);
      u8g2.print(a.acceleration.y);
      u8g2.drawStr(5, 40, "Z:");  
      u8g2.setCursor(20, 40);
      u8g2.print(a.acceleration.z);

      //gyro
      u8g2.setFont(u8g2_font_jinxedwizards_tr);
      u8g2.drawStr(60, 0, "X:");  
      u8g2.setCursor(80, 0);
      u8g2.print(g.gyro.x);
      u8g2.drawStr(60, 20, "Y:");  
      u8g2.setCursor(80, 20);
      u8g2.print(g.gyro.y);
      u8g2.drawStr(60, 40, "Z:");  
      u8g2.setCursor(80, 40);
      u8g2.print(g.gyro.z);
      // u8g2.sendBuffer();              
}


void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(SDA_PIN, SCL_PIN, I2C_FREQUENCY); //8 blue sda, 9 yellow scl

  if (!mpu.begin()) {
    Serial.println("MPU-6050 not found — run the I2C scanner first.");
    while (true) delay(1000);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G); //machine should not need to handle higher G force
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);      //slower smoother turns 
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);   //low pass filter 

  u8g2.begin();       //Initialize the function
  u8g2.setFontPosTop();  /*When you use drawStr to display strings, the default criteria is to display the lower-left
  * coordinates of the characters.The function can be understood as changing the coordinate position to the upper left
  * corner of the display string as the coordinate standard.*/
  }	  

void loop() {
  sensors_event_t a, g, t;
  uint32_t t0 = millis();
  mpu.getEvent(&a, &g, &t);           // acceleration in m/s^2

  Serial.printf("Accelerometer: %.2f,%.2f,%.2f\n",
                a.acceleration.x, a.acceleration.y, a.acceleration.z);

  Serial.printf("Gyro: %.2f,%.2f,%.2f\n",
              g.gyro.x, g.gyro.y, g.gyro.z);

  // delay(20);   50Hz sample rate of IMU (NOT currently verified)
  uint8_t counter = 0;
  uint32_t draw_cpu_us = 0;
  uint32_t td_total_start = micros();
  u8g2.firstPage(); // saves 896 bytes compared to full buffer 
  do {
    uint32_t t_draw = micros();
    draw(a, g); //drawing takes 32 ms
    draw_cpu_us += (micros() - t_draw); // Accumulate only draw() execution roughly 0.9ms per page 3.7ms total
  } while (u8g2.nextPage()); // goes through 8 pages, could pass up a mutex each time n pages is completed for sensor reading? 
  uint32_t total_display_us = micros() - td_total_start;
  uint32_t bus_transfer_us = total_display_us - draw_cpu_us; // around 30 ms

  Serial.printf("Display Total: %lu us | CPU Draw: %lu us | Bus Transfer: %lu us\n", 
                total_display_us, draw_cpu_us, bus_transfer_us);
	
  // delay(2000);
  uint32_t t1 = millis();
  uint32_t elapsed = t1 - t0;
  Serial.printf("Elapsed loop time: %d ms\n", elapsed);
} //total loop time is 53ms