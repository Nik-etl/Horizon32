#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <U8g2lib.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(/* rotation=*/U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

constexpr uint8_t SDA_PIN = 8; //blue
constexpr uint8_t SCL_PIN = 9; //yellow

// constexpr uint8_t MPU_address = 0x68;
// constexpr uint8_t OLED_address = 0x3c;

void draw(int a )
{
   switch(a)
   {
     case 0: 
     u8g2.setFont(u8g2_font_bubble_tr);    //Set the font set, which is“u8g2_font_bubble_tr”
     u8g2.drawStr(/* x=*/0,/* y=*/0, "Hi");       //Start drawing strings at the coordinates of x-0, y-0 “DFR123”
     u8g2.setFont(u8g2_font_lucasarts_scumm_subtitle_o_tf);
     u8g2.drawStr(0, 25, "Hi");  
     u8g2.setFont(u8g2_font_HelvetiPixelOutline_tr);
     u8g2.drawStr(0, 45, "Hi"); 
     break;            
     case 1: 
     u8g2.setFont(u8g2_font_tenstamps_mr);
     u8g2.drawStr(0,0, "Hi");  
     u8g2.setFont(u8g2_font_jinxedwizards_tr);
     u8g2.drawStr(56, 16, "Hi");  
     u8g2.setFont(u8g2_font_secretaryhand_tr);
     u8g2.drawStr(0, 32, "Hi"); 
     u8g2.setFont(u8g2_font_freedoomr10_mu);
     u8g2.drawStr(56, 48, "Hi"); 
     break;                     
   }	   
}


void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(8, 9); 

  // if (!mpu.begin()) {
  //   Serial.println("MPU-6050 not found — run the I2C scanner first.");
  //   while (true) delay(1000);
  // }
  // mpu.setAccelerometerRange(MPU6050_RANGE_8_G);

  u8g2.begin();       //Initialize the function
  u8g2.setFontPosTop();  /*When you use drawStr to display strings, the default criteria is to display the lower-left
  * coordinates of the characters.The function can be understood as changing the coordinate position to the upper left
  * corner of the display string as the coordinate standard.*/
  }	  

void loop() {
  // sensors_event_t a, g, t;
  // mpu.getEvent(&a, &g, &t);           // acceleration in m/s^2

  // Serial.printf("%.2f,%.2f,%.2f\n",
  //               a.acceleration.x, a.acceleration.y, a.acceleration.z);

  // delay(20);    //50Hz
  for( int i = 0; i <2 ; i++)
  {
   /*
	* firstPage will change the current page number position to 0
	* When modifications are between firstpage and nextPage, they will be re-rendered at each time.
	* This method consumes less ram space than sendBuffer
   */ 
    u8g2.firstPage();   
    do
	  {
		  draw(i);
	  } while( u8g2.nextPage() );
	  delay(2000);
  }
}