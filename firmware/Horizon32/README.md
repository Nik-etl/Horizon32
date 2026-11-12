## Useful notes for task planning:

* mutex to protect the i2c bus
* IMU reads can wait for draws
* Do not need to send full buffer at a time. 

# Task planning
**Sensor Task** higher priority
    - Read MPU, Calculation, write to global variable
    - deadline 20ms (50Hz satisfies nyquist theorem LPF = 21Hz, but 100Hz could be more optimal for calculations)
    - Attitude filter to git pitch, roll, and yaw
    - Could be closer to 100 Hz to make up for errors that could be caused from the sensor fusion
**Display Task** lower than sensor
    - Read variables, calculate graphic, update OLED 
    - deadline <100ms(10Hz) (20Hz optimal for crisp responsive graphics)
    - updateDisplayArea to split full display update into chunks and then pass mutex to let sensor run ~10ms

# Mutex 
Mutex corresponds with the I2C bus. The display task will grab the mutex write a part with updateDisplayArea() (10ms). 



# **Measurements**
sensor reading: < 1ms

# Page class measurements
cpu draw time: 3.7ms
    8 pages drawn: ~0.9ms for one page
bus transfer time: 30ms (About 7 seconds of overhead calculated 23ms)
total loop: 34-35ms

# Full buffer class measurements
drawing to buffer: 1.4ms
Total loop: 33ms-34ms

**Using UpdateDisplayArea**
Sends only portion of the buffer
1 tile (8x8 pixels): 0.6ms
1 row (128x8 pixels): 4ms
full transmission (128x64 pixels): 30ms
For only updating measurement displays: 22ms
updating acc values: 9.6ms
updating gyro values: 9.5ms