## Plantcare is an automatic plant watering system with water tank level monitoring via ESP32
* It will call a website of your choice when the water tank is empty so you can do whatever you want with the information like send yourself an e-mail or use MQTT or whatnot.
* It will call another website of your choice with the current humidity value as it changes so you can keep track of the humidity of your plant.
* Supports up to three humidity sensors and uses their average value for big pots.
* Automatically uses Push button WPS ( https://en.wikipedia.org/wiki/Wi-Fi_Protected_Setup ) after 20 seconds of not being able to connect to any Wifi network on ESP32 startup, e.g. on first use.

## Necessary Hardware
To build this you need an ESP32 (e.g. https://www.amazon.de/dp/B0D9LFM1MG ) as well as a humidity sensor, relais and pump (e.g. https://www.amazon.de/dp/B0814HXWVV ), a 3.9 Ohm resistor, a 47uF capacitor and maybe additional sensors (e.g. https://www.amazon.de/dp/B08GCRZVSR ). Also get a large USB power adapter that ideally uses USB-C and a good cable.
See the Fritzing image for how to wire everything.

## Necessary Software
Download https://code.jquery.com/jquery-3.7.1.min.js into the project directory.
Also see the top of the *.ino file for board package and libraries to install into the Arduino IDE.

## Possible Problems
* Make sure to open the project by double clicking the *.ino file in its directory or you may encounter an "Error: file not found: jquery-3.7.1.min.js" when compiling.
* If the power supply can not deliver enough power the ESP will brownout and restart and the pump will run several seconds longer.
