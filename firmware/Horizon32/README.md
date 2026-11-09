## Useful notes for task planning:

* mutex to protect the i2c bus
* IMU reads can wait for draws
* Do not need to send full buffer at a time. 

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
