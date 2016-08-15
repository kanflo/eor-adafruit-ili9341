# ILI9341 support for ESP Open RTOS

ILI9341 support for ESP Open RTOS ("EOR") using Adafruit's Arduino library. Since the library is MIT licensed it's safe to include in EOR. The included demo program is, among other things, a wifi photo viewer you can upload images to.

**Howto**

Assuming you have cloned the EOR git and set your wifi credentials:

```
export EOR_ROOT=/path/to/esp-open-rtos
cd $EOR_ROOT
git clone https://github.com/kanflo/eor-arduino-compat.git extras/arduino-compat
git clone https://github.com/kanflo/eor-adafruit-ili9341.git extras/ili9341
git clone https://github.com/kanflo/eor-cli.git extras/cli
cd extras/ili9341/example
make -j8 && make flash
```
Check the UART output for the IP address and then try

```
cat sunset.bmp | nc -w1 <ip address> 80
```

Or try ```help``` in the UART command line interface.

Note that image upload using ```nc``` should take less than 1s. If it takes longer, try adding the following line to ```lwip/include/lwipopts.h``` and rebuild:

```
#define TCP_WND (TCP_MSS * 2)
```

See [EOR issue #151](https://github.com/SuperHouse/esp-open-rtos/issues/151) for further information.

**Changes compared to the Adafruit master**

The changes to the original Adafruit source files are

* Renamed *.c -> *.cpp
* *.h -> *.hpp
* Changed SPI. -> SPIc::
* Changed write(...) to writec(...) as write is an LWIP macro
* Inclusion of eor_arduino_compat.hpp

The ILI9341 and GFX libraries are copyright (c) 2013 Adafruit Industries. All rights reserved.
