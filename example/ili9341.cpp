/* ILI9341 demo
 *
 * This sample code is in the public domain.
 */

#include <stdlib.h>
#include <espressif/esp_common.h>
#include <esp/uart.h> 
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <esp8266.h>
#include <esp/spi.h>
#include <ssid_config.h>

#include <lwip/tcp.h>
#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>

extern "C" {
  #include <ota-tftp.h>
  #include <cli.h>
  #include "zlatni_rat.h"
}

#include "Adafruit_GFX.hpp"
#include "Adafruit_ILI9341.hpp"
#include "SPI.hpp"



struct netif    netif_data;

const char testPage[] = "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html\r\n"
                        "Connection: Close\r\n\r\n"
                        "<html>"
                        "<head>"
                        "<title>ESP8266 test page</title>"
                        "<style type='text/css'>"
                        "body{font-family:'Arial, sans-serif', sans-serif;font-size:.8em;background-color:#fff;}"
                        "</style>"
                        "</head>"
                        "<body>%s</body></html>\r\n\r\n";

char buffer[1024];
char temp_buf[1024];



#define TFT_CS 4
#define TFT_DC 2
#define TFT_LED 0

#define ADC_BUTTON_PRESSED (1000) // adc > ADC_BUTTON_PRESSED when button pressed

xSemaphoreHandle gTFTSemaphore;
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

#define delay_ms(ms) vTaskDelay(ms / portTICK_RATE_MS)

extern "C" {

// Convert a 24-bit RGB color to an equivalent 16-bit RGB565 value
static __inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
//    return ((r / 8) << 11) | ((g / 4) << 5) | (b / 8);
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static __inline uint16_t read16(uint8_t *bmp, uint32_t *pos)
{
    uint16_t result;
    ((uint8_t *)&result)[0] = bmp[(*pos)++];  // LSB
    ((uint8_t *)&result)[1] = bmp[(*pos)++];  // MSB
    return result;
}

static __inline uint32_t read32(uint8_t *bmp, uint32_t *pos)
{
    uint32_t result;
    ((uint8_t *)&result)[0] = bmp[(*pos)++];  // LSB
    ((uint8_t *)&result)[1] = bmp[(*pos)++];
    ((uint8_t *)&result)[2] = bmp[(*pos)++];
    ((uint8_t *)&result)[3] = bmp[(*pos)++];  // MSB
    return result;
}

#define NO_COLOR 0xbaadc01a

static bool parsing_head = true;
static bool parser_x, parser_y;
static uint32_t parser_r, parser_g, parser_b;

static void bmp_parser_reset(uint32_t x, uint32_t y)
{
    parsing_head = true;
    parser_x = x;
    parser_y = y;
    parser_r = parser_g = parser_b = NO_COLOR;
}


static bool bmp_parser(char *data, uint32_t len)
{
    uint8_t *bmp = (uint8_t*) data;
    uint32_t pos = 0;
    int      bmp_width, bmp_height;   // W+H in pixels
    uint8_t  bmp_depth;               // Bit depth (currently must be 24)
    uint32_t img_offset;              // Start of image data in file
    uint32_t img_size;
    bool success = true;

    if (!data || !len) {
        printf("BMP parser got null data\n");
        return false;
    }

    do {
        if (parsing_head) {
            if (read16(bmp, &pos) == 0x4D42) { // BMP signature
                img_size = read32(bmp, &pos);
                (void) read32(bmp, &pos); // Read & ignore creator bytes
                img_offset = read32(bmp, &pos); // Start of image data
                // Read DIB header
                (void) read32(bmp, &pos); // Header size
                bmp_width  = read32(bmp, &pos);
                bmp_height = read32(bmp, &pos);
                if (read16(bmp, &pos) == 1) { // # planes -- must be '1'
                    bmp_depth = read16(bmp, &pos); // bits per pixel
                    if ((bmp_depth == 24) && (read32(bmp, &pos) == 0)) { // 0 = uncompressed
                        if (bmp_height != tft.height() || bmp_width != tft.width()) {
                            printf("BMP size does not match display (w:%d h:%d)\n", tft.width(), tft.height());
                            success = false;
                            break;
                        }
                        printf("Image dimensions: %d x %d  %d bit  %d bytes (offset %d)\n", bmp_width, bmp_height, bmp_depth, img_size, img_offset);
                        // BMP rows are padded (if needed) to 4-byte boundary
                        parsing_head = false;
                        tft.setAddrWindow(parser_x, parser_y, parser_x+bmp_width-1, parser_y+bmp_height-1);
                        gpio_write(TFT_DC, true); // @todo: should be done in setAddrWindow imho
                        pos = img_offset;
                        continue;
                    } else {
                        printf("Bad BMP!\n");
                        success = false;
                    }
                }
            } else {
                printf("Wrong BMP magic!\n");
                success = false;
            }
        } else {
            if (parser_b == NO_COLOR && pos < len) {
                parser_b = bmp[pos++];
            }
            if (parser_g == NO_COLOR && pos < len) {
                parser_g = bmp[pos++];
            }
            if (parser_r == NO_COLOR && pos < len) {
                parser_r = bmp[pos++];
            }
            if (parser_r != NO_COLOR && parser_g != NO_COLOR && parser_b != NO_COLOR) {
                uint16_t temp = ((parser_r / 8) << 11) | ((parser_g / 4) << 5) | (parser_b / 8);
                temp = temp >> 8 | temp << 8;
                gpio_write(TFT_CS, false);
                (void) spi_transfer_16(1, temp); // Faster than tft.pushColor(rgb565(r,g,b));
                gpio_write(TFT_CS, true);
                parser_r = parser_g = parser_b = NO_COLOR;
            } else {
//                printf("Not enough pixel data %d %d\n", pos, len);
            }
        }
    } while(pos < len && success);
    return success;
}

static void bmp_draw(uint8_t *bmp, uint16_t x, uint16_t y)
{
    uint32_t bmp_pos = 0;
    int      bmp_width, bmp_height;   // W+H in pixels
    uint8_t  bmp_depth;               // Bit depth (currently must be 24)
    uint32_t img_offset;              // Start of image data in file
    uint32_t row_size;                // Not always = bmp_width; may have padding
    boolean  good_bmp = false;        // Set to true on valid header parse
    boolean  flip    = true;          // BMP is stored bottom-to-top
    int      w, h, row, col;
    uint8_t  r, g, b;
    uint32_t pos = 0;
    uint32_t img_size;

    if ((x >= tft.width()) || (y >= tft.height())) {
        return;
    }

    xSemaphoreTake(gTFTSemaphore, portMAX_DELAY);
    // Parse BMP header
    if (read16(bmp, &bmp_pos) == 0x4D42) { // BMP signature
        img_size = read32(bmp, &bmp_pos);
        (void) read32(bmp, &bmp_pos); // Read & ignore creator bytes
        img_offset = read32(bmp, &bmp_pos); // Start of image data
        // Read DIB header
        (void) read32(bmp, &bmp_pos); // Header size
        bmp_width  = read32(bmp, &bmp_pos);
        bmp_height = read32(bmp, &bmp_pos);
        if (read16(bmp, &bmp_pos) == 1) { // # planes -- must be '1'
            bmp_depth = read16(bmp, &bmp_pos); // bits per pixel
            if ((bmp_depth == 24) && (read32(bmp, &bmp_pos) == 0)) { // 0 = uncompressed
                good_bmp = true; // Supported BMP format -- proceed!
                printf("Image dimensions: %d x %d  %d bit  %d bytes\n", bmp_width, bmp_height, bmp_depth, img_size);
                // BMP rows are padded (if needed) to 4-byte boundary
                row_size = (bmp_width * 3 + 3) & ~3;

                // If bmp_height is negative, image is in top-down order.
                // This is not common but has been observed in the wild.
                if (bmp_height < 0) {
                    bmp_height = -bmp_height;
                    flip = false;
                }

                // Crop area to be loaded
                w = bmp_width;
                h = bmp_height;
                if ((x+w-1) >= tft.width())  w = tft.width()  - x;
                if ((y+h-1) >= tft.height()) h = tft.height() - y;

                // Set TFT address window to clipped image bounds
                tft.setAddrWindow(x, y, x+w-1, y+h-1);

                gpio_write(TFT_DC, true); // @todo: should be done in setAddrWindow imho
                gpio_write(TFT_CS, false);
                for (row=0; row<h; row++) { // For each scanline...

                    // Seek to start of scan line.  It might seem labor-
                    // intensive to be doing this on every line, but this
                    // method covers a lot of gritty details like cropping
                    // and scanline padding.  Also, the seek only takes
                    // place if the file position actually needs to change
                    // (avoids a lot of cluster math in SD library).
                    if (flip) // Bitmap is stored bottom-to-top order (normal BMP)
                        pos = img_offset + (bmp_height - 1 - row) * row_size;
                    else     // Bitmap is stored top-to-bottom
                        pos = img_offset + row * row_size;
                    for (col=0; col<w; col++) { // For each pixel...
                        // Convert pixel from BMP to TFT format, push to display
                        b = bmp[pos++];
                        g = bmp[pos++];
                        r = bmp[pos++];
                        uint16_t temp = ((r / 8) << 11) | ((g / 4) << 5) | (b / 8);
                        temp = temp >> 8 | temp << 8;
                        (void) spi_transfer_16(1, temp); // Faster than tft.pushColor(rgb565(r,g,b));
                    }
                }
                gpio_write(TFT_CS, true);
            }
        }
    }

    if (!good_bmp) {
        printf("BMP format not recognized.\n");
    }
    xSemaphoreGive(gTFTSemaphore);
}


void init_cmd(uint32_t argc, char *argv[])
{
    uint8_t num_attemtps = 3;
    uint8_t diag;
    spi_init(1, (spi_mode_t) SPI_MODE0, SPI_FREQ_DIV_20M, true, SPI_LITTLE_ENDIAN, true);

    /*
    SPI_FREQ_DIV_2M   < 2MHz
    SPI_FREQ_DIV_4M   < 4MHz
    SPI_FREQ_DIV_8M   < 8MHz
    SPI_FREQ_DIV_10M  < 10MHz
    SPI_FREQ_DIV_20M  < 20MHz
    */

    xSemaphoreTake(gTFTSemaphore, portMAX_DELAY);
    do {
        tft.begin();
        tft.setCursor(0, 0);
        tft.setTextColor(ILI9341_WHITE);
        diag = tft.readcommand8(ILI9341_RDSELFDIAG);
        num_attemtps--;        
    } while(diag != 0xc0 && num_attemtps); // Sometimes the tft init fails and diag reads 0

    if (diag != 0xc0) {
        printf("Error! ILI9341 init failed.\n");
    } else {
        // Read diagnostics (optional but can help debug problems)
        printf("Display Power Mode: 0x%02x\n", tft.readcommand8(ILI9341_RDMODE));
        printf("MADCTL Mode:        0x%02x\n", tft.readcommand8(ILI9341_RDMADCTL));
        printf("Pixel Format:       0x%02x\n", tft.readcommand8(ILI9341_RDPIXFMT));
        printf("Image Format:       0x%02x\n", tft.readcommand8(ILI9341_RDIMGFMT));
        printf("Self Diagnostic:    0x%02x\n", tft.readcommand8(ILI9341_RDSELFDIAG));
    }
    xSemaphoreGive(gTFTSemaphore);
}

void draw_bmp_cmd(uint32_t argc, char *argv[])
{
    bmp_draw((uint8_t*) zlatni_rat_bmp, 0, 0);
}

void fill_cmd(uint32_t argc, char *argv[])
{
    char temp[3] = {0, 0, 0};
    char *end;
    uint8_t r, g, b;
    if (strlen(argv[1]) != 7) {
        printf("Express color as #rrggbb\n");
    } else {
        xSemaphoreTake(gTFTSemaphore, portMAX_DELAY);
        temp[0] = argv[1][1];
        temp[1] = argv[1][2];
        r = (uint8_t) strtoul((char*) &temp, &end, 16);
        temp[0] = argv[1][3];
        temp[1] = argv[1][4];
        g = (uint8_t) strtoul((char*) &temp, &end, 16);
        temp[0] = argv[1][5];
        temp[1] = argv[1][6];
        b = (uint8_t) strtoul((char*) &temp, &end, 16);
        tft.fillScreen(rgb565(r, g, b));
        xSemaphoreGive(gTFTSemaphore);
    }
}

void text_cmd(uint32_t argc, char *argv[])
{
    xSemaphoreTake(gTFTSemaphore, portMAX_DELAY);
    if (argc == 1) {
       tft.println((char*) "");
    }
    for (uint32_t i = 1; i < argc; i++) {
        tft.print(argv[i]);
        tft.print((char*) " ");
    }
    xSemaphoreGive(gTFTSemaphore);
}

void text_size_cmd(uint32_t argc, char *argv[])
{
    xSemaphoreTake(gTFTSemaphore, portMAX_DELAY);
    tft.setTextSize(atoi(argv[1]));
    xSemaphoreGive(gTFTSemaphore);
}

void cls_cmd(uint32_t argc, char *argv[])
{
    xSemaphoreTake(gTFTSemaphore, portMAX_DELAY);
    tft.fillScreen(ILI9341_BLACK);
    tft.setCursor(0, 0);
    xSemaphoreGive(gTFTSemaphore);
}

void on_cmd(uint32_t argc, char *argv[])
{
    xSemaphoreTake(gTFTSemaphore, portMAX_DELAY);
    for (uint32_t i=1; i<argc; i++) {
        uint32_t gpio = atoi(argv[i]);
        printf(" Turning on GPIO %d\n", gpio);
        gpio_enable(gpio, GPIO_OUTPUT);
        gpio_write(gpio, true);
    }
    xSemaphoreGive(gTFTSemaphore);
}

void off_cmd(uint32_t argc, char *argv[])
{
    xSemaphoreTake(gTFTSemaphore, portMAX_DELAY);
    for (uint32_t i=1; i<argc; i++) {
        uint32_t gpio = atoi(argv[i]);
        printf(" Turning off GPIO %d\n", gpio);
        gpio_enable(gpio, GPIO_OUTPUT);
        gpio_write(gpio, false);
    }
    xSemaphoreGive(gTFTSemaphore);
}

err_t recv_callback(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
//    struct netif  *netif = &netif_data;
    char *data;
    if (err == ERR_OK && p != NULL) { // Check if status is ok and data is arrived.
 //       printf("recv %d bytes from %d.%d.%d.%d\r\n", p->tot_len, ip4_addr1(&(pcb->remote_ip)), ip4_addr2(&(pcb->remote_ip)), ip4_addr3(&(pcb->remote_ip)), ip4_addr4(&(pcb->remote_ip)));
        tcp_recved(pcb, p->tot_len); // Inform TCP that we have taken the data.
        data = static_cast<char *>(p->payload);
        if (strncmp(data, "GET ", 4) == 0) {
            struct ip_info ipconfig;
            (void) sdk_wifi_get_ip_info(STATION_IF, &ipconfig);
            printf("Handling GET request...\n");
            printf("Request:\n%s\n", data);
            snprintf(temp_buf, sizeof(temp_buf),
                "<h1>w00t!</h1>If you can see this page, your ESP8266 is working properly."
                "<p>Open a terminal window and try <pre>cat sunset.bmp | nc -w1 %d.%d.%d.%d 80</pre>",
                ip4_addr1(&ipconfig), ip4_addr2(&ipconfig), ip4_addr3(&ipconfig), ip4_addr4(&ipconfig));
            sprintf(buffer, testPage, temp_buf);
            if (tcp_write(pcb, (void *)buffer, strlen(buffer), 1) == ERR_OK) {
                tcp_output(pcb);
                printf("Closing connection...\n");
                tcp_close(pcb);
            }
        } else {
            if (false == bmp_parser(data, p->tot_len)) {
                strncpy((char*) buffer, "Error. Not a valid BMP, the image must be 240px wide and 320px tall.\n", sizeof(buffer));
                if (tcp_write(pcb, (void *)buffer, strlen(buffer), 1) == ERR_OK) {
                    tcp_output(pcb);
                }
                xSemaphoreGive(gTFTSemaphore);
                tcp_close(pcb);
            }
        }
        pbuf_free(p);
    } else {
        // No data arrived indicating the client closed the connection and
        // sent us a packet with FIN flag set to 1 and we need to cleanup and
        // destroy our TCP connection.
        printf("Connection closed by client.\n");
        pbuf_free(p);
        xSemaphoreGive(gTFTSemaphore);
    }
    return ERR_OK;
}

// Accept an incomming call on the registered port
err_t accept_callback(void *arg, struct tcp_pcb *pcb, err_t err)
{
    LWIP_UNUSED_ARG(arg);
    // Register receive callback function
    printf("TCP accept from %d.%d.%d.%d\n", ip4_addr1(&(pcb->remote_ip)),ip4_addr2(&(pcb->remote_ip)),ip4_addr3(&(pcb->remote_ip)),ip4_addr4(&(pcb->remote_ip)));
    xSemaphoreTake(gTFTSemaphore, portMAX_DELAY);
    bmp_parser_reset(0, 0);
    tcp_recv(pcb, &recv_callback);
    return ERR_OK;
}

void server_task(void *pvParameters)
{
    // Bind a function to a tcp port
    struct tcp_pcb *pcb = tcp_new();
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) == ERR_OK) {
        pcb = tcp_listen(pcb);
        tcp_accept(pcb, &accept_callback);
    }

    printf("Waiting for connection...\n");
    while(1) {
        delay_ms(1000);
    }
}

void button_task(void *pvParameters)
{
    struct ip_info ipconfig;
    char msg[32];
    while(1) {
        while (sdk_system_adc_read() < ADC_BUTTON_PRESSED) {
            delay(100);
        }

        if (!sdk_wifi_get_ip_info(STATION_IF, &ipconfig)) {
            printf("Failed to read my own IP address...\n");
            snprintf((char*) msg, sizeof(msg), "No IP");
        } else {

        }
        snprintf((char*) msg, sizeof(msg), "%d.%d.%d.%d", ip4_addr1(&ipconfig), ip4_addr2(&ipconfig), ip4_addr3(&ipconfig), ip4_addr4(&ipconfig));
        xSemaphoreTake(gTFTSemaphore, portMAX_DELAY);
        tft.fillRect(0, 0, 320, 25, ILI9341_BLACK);
        tft.setTextSize(2);
        tft.setCursor(5, 5);
        tft.print((char*) msg);
        xSemaphoreGive(gTFTSemaphore);

        while (sdk_system_adc_read() >= ADC_BUTTON_PRESSED) {
            delay(250);
        }
    }
}

void cli_task(void *pvParameters) {
    const command_t cmds[] = {
        { .cmd = "init",  .handler = &init_cmd,        .min_arg = 0, .max_arg = 0,   .help = "ILI9341 init" },
        { .cmd = "fill",  .handler = &fill_cmd,        .min_arg = 1, .max_arg = 1,   .help = "Fill screen with specified color", .usage = "#rrggbb" },
        { .cmd = "bmp",   .handler = &draw_bmp_cmd,    .min_arg = 0, .max_arg = 0,   .help = "Draw BMP on screen" },
        { .cmd = "cls",   .handler = &cls_cmd,         .min_arg = 0, .max_arg = 0,   .help = "Clear screen" },
        { .cmd = "t",     .handler = &text_cmd,        .min_arg = 0, .max_arg = 16,  .help = "Draw text on screen. eg. 't Hello World!' or 't' for newline" },
        { .cmd = "size",  .handler = &text_size_cmd,   .min_arg = 1, .max_arg = 11,  .help = "Set text size",  .usage = "<1...>" },
        { .cmd = "on",    .handler = &on_cmd,          .min_arg = 1, .max_arg = 16,  .help = "Turn on one or more GPIOs", .usage = "<gpio> [<gpio>]*" },
        { .cmd = "off",   .handler = &off_cmd,         .min_arg = 1, .max_arg = 16,  .help = "Turn off one or more GPIOs", .usage = "<gpio> [<gpio>]*" },
    };
    delay_ms(250); // Seem to run into problems it initing the TFT too soon
    init_cmd(0, 0);
    draw_bmp_cmd(0, 0);
    gpio_write(TFT_LED, false);
    xTaskCreate(button_task, (signed char *)"button_task", 512, NULL, 2, NULL);
    cli_run(cmds, sizeof(cmds) / sizeof(command_t), "the ILI9341 demo");
}

void user_init(void)
{
    gpio_enable(TFT_LED, GPIO_OUTPUT);
    gpio_write(TFT_LED, true);
    uart_set_baud(0, 115200);
    vSemaphoreCreateBinary(gTFTSemaphore);

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
    xTaskCreate(server_task, (signed char *)"server_task", 512, NULL, 2, NULL);
}

} // extern "C"
