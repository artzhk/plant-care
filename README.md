# Plant Care
The plant care system is designed to build an interactive garden in your home, by providing ambient lightning or any
other triggering mechanism to do not forget to take care of plants for people with Altheimers and other brain deseases.

The current state is very bare prototype that displays possibilities and basic project structure. 

Additionally the project server as know-how integrations with HA(Home assistant) OS and between nodes communication. 

The design could be improved and simplifed, at the moment of creation I didn't have an LCD with I2C or additional OLED
for master node, that is why I've built an I2C interface out of nano board.

# Architecture overview

```
                      +------------------------+
Node Sensor - push -> | HA (home assistant) OS | 
                      +------------------------+
Collects                     ^                                 
data from                    |                                
sensors                   subscribes                        
                             |        
                        Master Node - serving as dashboard of collected data
```

Home assistant integration allows for automations, notifications and intergrations with other systems and hardware. 

# Components overview
[Node Sensor](https://app.cirkitdesigner.com/project/ae44ae01-049e-4d11-9962-7d177c8b3517):
+ 1 CJMCU GUVA S12SD Ultraviolet Sensor.
+ 1 SW390 Soil Moisture V2 Sensor.
+ 1 CD74HC4067 analog mux/dmux.
+ 1 ESP8266 nodemcuv2.
+ 1 RGB LED.
+ 1 OLED via I2C.
+ 1 DHT11.
+ 3 220 Ohm resistors

[Master Node](https://app.cirkitdesigner.com/project/af9f84e4-3372-4d50-b3e4-fac91a83b9a2):
+ 1 voltage shifter.
+ 1 Arduino Nano.
+ 1 LCD display. [Setup](https://docs.arduino.cc/learn/electronics/lcd-displays/)
+ 2 10k Ohm potentiometer.
+ 1 220 Ohm resistor
+ 1 ESP8266 nodemcuv2.
+ 1 Switch.
