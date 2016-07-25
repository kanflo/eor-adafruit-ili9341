# ILI9341 support for ESP Open RTOS

ILI9341 support for ESP Open RTOS using Adafruit's Arduino library. Since the library is MIT licensed it's safe to include in EOR. 

Cloning & building:

```
export EOR_ROOT=/path/to/esp-open.rtos
cd $EOR_ROOT
git clone https://github.com/kanflo/eor-arduino-compat.git extras/arduino-compat
git clone https://github.com/kanflo/eor-adafruit-ili9341.git extras/ili9341
git clone https://github.com/kanflo/eor-cli.git extras/cli
cd extras/ili9341/example
make -j8 && make flash
```

The only changes to the original source files are

* Renamed *.c -> *.cpp
* *.h -> *.hpp
* Changed SPI. -> SPIc::
* Inclusion of eor_arduino_compat.hpp

The library is copyright (c) 2013 Adafruit Industries. All rights reserved.

