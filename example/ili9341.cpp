/* ILI9341 demo (a boring one)
 *
 * This sample code is in the public domain.
 */

#include <stdlib.h>
#include <espressif/esp_common.h>
#include <esp/uart.h> 
#include <FreeRTOS.h>
#include <task.h>
#include <esp8266.h>
#include <esp/spi.h>
#include <ssid_config.h>

extern "C" {
  #include <ota-tftp.h>
  #include <cli.h>
}

#include "Adafruit_GFX.hpp"
#include "Adafruit_ILI9341.hpp"
#include "SPI.hpp"


#define TFT_CS 4
#define TFT_DC 2
#define TFT_LED 0

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

extern "C" {
/**************************************************************************/
/*!
    @brief  Converts a 24-bit RGB color to an equivalent 16-bit RGB565 value

    @param[in]  r  8-bit red
    @param[in]  g  8-bit green
    @param[in]  b  8-bit blue

    @section Example

    @code 

    // Get 16-bit equivalent of 24-bit color
    uint16_t gray = drawRGB24toRGB565(0x33, 0x33, 0x33);

    @endcode
*/
/**************************************************************************/
uint16_t drawRGB24toRGB565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r / 8) << 11) | ((g / 4) << 5) | (b / 8);
}

void init_cmd(uint32_t argc, char *argv[])
{
  spi_init(1, (spi_mode_t) SPI_MODE0, SPI_FREQ_DIV_20M, true, SPI_LITTLE_ENDIAN, true);

/*
SPI_FREQ_DIV_2M   < 2MHz
SPI_FREQ_DIV_4M   < 4MHz
SPI_FREQ_DIV_8M   < 8MHz
SPI_FREQ_DIV_10M  < 10MHz
SPI_FREQ_DIV_20M  < 20MHz
*/

  gpio_enable(TFT_LED, GPIO_OUTPUT);
  gpio_write(TFT_LED, false);

  printf(" TFT begin\n");
  tft.begin();
  printf("     fillScreen\n");
  tft.fillScreen(ILI9341_GREEN);
  printf("     done\n");
}

void fill_red_cmd(uint32_t argc, char *argv[])
{
  printf(" TFT fillScreen\n");
  tft.fillScreen(ILI9341_RED);
  printf("     done\n");
}

void fill_green_cmd(uint32_t argc, char *argv[])
{
  printf(" TFT fillScreen\n");
  tft.fillScreen(ILI9341_GREEN);
  printf("     done\n");
}

void fill_blue_cmd(uint32_t argc, char *argv[])
{
  printf(" TFT fillScreen\n");
  tft.fillScreen(ILI9341_BLUE);
  printf("     done\n");
}

 void on_cmd(uint32_t argc, char *argv[])
{
  for (uint32_t i=1; i<argc; i++) {
    uint32_t gpio = atoi(argv[i]);
    printf(" Turning on GPIO %d\n", gpio);
    gpio_enable(gpio, GPIO_OUTPUT);
    gpio_write(gpio, true);

  }
}

void off_cmd(uint32_t argc, char *argv[])
{
  for (uint32_t i=1; i<argc; i++) {
    uint32_t gpio = atoi(argv[i]);
    printf(" Turning off GPIO %d\n", gpio);
    gpio_enable(gpio, GPIO_OUTPUT);
    gpio_write(gpio, false);
  }
}

void cli_task(void *pvParameters) {
  const command_t cmds[] = {
    { .cmd = "i",     .handler = &init_cmd,        .min_arg = 0, .max_arg = 0,   .help = "ILI9341 init", },
    { .cmd = "r",     .handler = &fill_red_cmd,    .min_arg = 0, .max_arg = 0,   .help = "Fill screen with red", },
    { .cmd = "g",     .handler = &fill_green_cmd,  .min_arg = 0, .max_arg = 0,   .help = "Fill screen with green", },
    { .cmd = "b",     .handler = &fill_blue_cmd,   .min_arg = 0, .max_arg = 0,   .help = "Fill screen with blue", },
    { .cmd = "on",    .handler = &on_cmd,          .min_arg = 1, .max_arg = 16,  .help = "Turn on one or more GPIOs",   .usage = "<gpio> [<gpio>]*", },
    { .cmd = "off",   .handler = &off_cmd,         .min_arg = 1, .max_arg = 16,  .help = "Turn off one or more GPIOs",  .usage = "<gpio> [<gpio>]*", }
  };
  cli_run(cmds, sizeof(cmds) / sizeof(command_t), "the ILI9341 demo");
}

void user_init(void)
{
    uart_set_baud(0, 115200);

#ifndef CONFIG_NO_WIFI
  // Wifi not necessary for the CLI demo but I use OTA for flashing
    ota_tftp_init_server(TFTP_PORT);
    struct sdk_station_config config;
    strcpy((char*) &config.ssid, (char*) WIFI_SSID);
    strcpy((char*) &config.password, (char*) WIFI_PASS);
    // required to call wifi_set_opmode before station_set_config
    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&config);
#endif // CONFIG_NO_WIFI

    xTaskCreate(cli_task, (signed char *)"cli_task", 512, NULL, 2, NULL);
}

} // extern "C"