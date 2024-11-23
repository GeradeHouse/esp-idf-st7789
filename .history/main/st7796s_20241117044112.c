#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <driver/spi_master.h>
#include <driver/gpio.h>
#include "esp_log.h"

#include "st7796s.h"  // Updated to the correct header file

#define TAG "ST7796S"
#define _DEBUG_ 0

#if CONFIG_SPI2_HOST
#define HOST_ID SPI2_HOST
#elif CONFIG_SPI3_HOST
#define HOST_ID SPI3_HOST
#endif

static const int SPI_Command_Mode = 0;
static const int SPI_Data_Mode = 1;
static const int SPI_Frequency = SPI_MASTER_FREQ_40M;

void spi_master_init(TFT_t *dev, int16_t GPIO_MOSI, int16_t GPIO_SCLK, int16_t GPIO_CS,
                     int16_t GPIO_DC, int16_t GPIO_RESET, int16_t GPIO_BL) {
    esp_err_t ret;

    ESP_LOGI(TAG, "GPIO_CS=%d", GPIO_CS);
    if (GPIO_CS >= 0) {
        gpio_reset_pin(GPIO_CS);
        gpio_set_direction(GPIO_CS, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_CS, 1);
    }

    ESP_LOGI(TAG, "GPIO_DC=%d", GPIO_DC);
    gpio_reset_pin(GPIO_DC);
    gpio_set_direction(GPIO_DC, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_DC, 0);

    ESP_LOGI(TAG, "GPIO_RESET=%d", GPIO_RESET);
    if (GPIO_RESET >= 0) {
        gpio_reset_pin(GPIO_RESET);
        gpio_set_direction(GPIO_RESET, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_RESET, 1);  // Ensure the reset pin starts high
        delayMS(100);
        gpio_set_level(GPIO_RESET, 0);  // Pulse reset pin
        delayMS(100);
        gpio_set_level(GPIO_RESET, 1);  // Set back to high
        delayMS(100);
    }

    ESP_LOGI(TAG, "GPIO_BL=%d", GPIO_BL);
    if (GPIO_BL >= 0) {
        gpio_reset_pin(GPIO_BL);
        gpio_set_direction(GPIO_BL, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_BL, 1);  // Set GPIO_BL high to turn on the backlight
        ESP_LOGI(TAG, "Backlight enabled on GPIO_BL=%d", GPIO_BL);
    }

    ESP_LOGI(TAG, "GPIO_MOSI=%d", GPIO_MOSI);
    ESP_LOGI(TAG, "GPIO_SCLK=%d", GPIO_SCLK);
    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 6 * 1024,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };

    ret = spi_bus_initialize(HOST_ID, &buscfg, SPI_DMA_CH_AUTO);
    ESP_LOGI(TAG, "spi_bus_initialize=%d", ret);
    assert(ret == ESP_OK);

    spi_device_interface_config_t devcfg;
    memset(&devcfg, 0, sizeof(devcfg));
    devcfg.clock_speed_hz = SPI_Frequency;
    devcfg.queue_size = 7;
    devcfg.mode = 0;  // Changed from 2 to 0
    devcfg.flags = SPI_DEVICE_NO_DUMMY;

    if (GPIO_CS >= 0) {
        devcfg.spics_io_num = GPIO_CS;
    } else {
        devcfg.spics_io_num = -1;
    }

    spi_device_handle_t handle;
    ret = spi_bus_add_device(HOST_ID, &devcfg, &handle);
    ESP_LOGI(TAG, "spi_bus_add_device=%d", ret);
    assert(ret == ESP_OK);

    dev->_dc = GPIO_DC;
    dev->_bl = GPIO_BL;
    dev->_SPIHandle = handle;
}

bool spi_master_write_byte(spi_device_handle_t SPIHandle, const uint8_t *Data, size_t DataLength) {
    spi_transaction_t SPITransaction;
    esp_err_t ret;

    if (DataLength > 0) {
        memset(&SPITransaction, 0, sizeof(spi_transaction_t));
        SPITransaction.length = DataLength * 8;
        SPITransaction.tx_buffer = Data;
        ret = spi_device_polling_transmit(SPIHandle, &SPITransaction);
        assert(ret == ESP_OK);
    }

    return true;
}

bool spi_master_write_command(TFT_t *dev, uint8_t cmd) {
    uint8_t Byte = cmd;
    gpio_set_level(dev->_dc, SPI_Command_Mode);
    return spi_master_write_byte(dev->_SPIHandle, &Byte, 1);
}

bool spi_master_write_data_byte(TFT_t *dev, uint8_t data) {
    uint8_t Byte = data;
    gpio_set_level(dev->_dc, SPI_Data_Mode);
    return spi_master_write_byte(dev->_SPIHandle, &Byte, 1);
}

bool spi_master_write_data_word(TFT_t *dev, uint16_t data) {
    uint8_t Byte[2];
    Byte[0] = (data >> 8) & 0xFF;
    Byte[1] = data & 0xFF;
    gpio_set_level(dev->_dc, SPI_Data_Mode);
    return spi_master_write_byte(dev->_SPIHandle, Byte, 2);
}

bool spi_master_write_addr(TFT_t *dev, uint16_t addr1, uint16_t addr2) {
    uint8_t Byte[4];
    Byte[0] = (addr1 >> 8) & 0xFF;
    Byte[1] = addr1 & 0xFF;
    Byte[2] = (addr2 >> 8) & 0xFF;
    Byte[3] = addr2 & 0xFF;
    gpio_set_level(dev->_dc, SPI_Data_Mode);
    return spi_master_write_byte(dev->_SPIHandle, Byte, 4);
}

bool spi_master_write_color(TFT_t *dev, uint16_t color, uint32_t size) {
    uint8_t Byte[1024];
    uint32_t len = size * 2;
    uint32_t max_chunk = sizeof(Byte);

    while (len > 0) {
        uint32_t chunk_size = (len > max_chunk) ? max_chunk : len;
        uint32_t color_count = chunk_size / 2;
        for (uint32_t i = 0; i < color_count; i++) {
            Byte[i * 2] = (color >> 8) & 0xFF;
            Byte[i * 2 + 1] = color & 0xFF;
        }
        gpio_set_level(dev->_dc, SPI_Data_Mode);
        spi_master_write_byte(dev->_SPIHandle, Byte, chunk_size);
        len -= chunk_size;
    }
    return true;
}

// Add 202001
bool spi_master_write_colors(TFT_t *dev, uint16_t *colors, uint16_t size) {
    uint8_t Byte[1024];
    uint32_t index = 0;
    uint32_t len = size * 2;
    uint32_t max_chunk = sizeof(Byte);

    while (len > 0) {
        uint32_t chunk_size = (len > max_chunk) ? max_chunk : len;
        uint32_t color_count = chunk_size / 2;
        for (uint32_t i = 0; i < color_count; i++) {
            Byte[i * 2] = (colors[index] >> 8) & 0xFF;
            Byte[i * 2 + 1] = colors[index] & 0xFF;
            index++;
        }
        gpio_set_level(dev->_dc, SPI_Data_Mode);
        spi_master_write_byte(dev->_SPIHandle, Byte, chunk_size);
        len -= chunk_size;
    }
    return true;
}

void delayMS(int ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void lcdInit(TFT_t *dev, int width, int height, int offsetx, int offsety) {
    dev->_width = width;
    dev->_height = height;
    dev->_offsetx = offsetx;
    dev->_offsety = offsety;
    dev->_font_direction = DIRECTION0;
    dev->_font_fill = false;
    dev->_font_underline = false;

    ESP_LOGI(TAG, "Initializing ST7796S LCD");

    // Hardware reset
    if (dev->_reset >= 0) {
        gpio_set_level(dev->_reset, 0); // Set RESX low
        delayMS(20);                   // Minimum 10ms
        gpio_set_level(dev->_reset, 1); // Set RESX high
        delayMS(120);                  // Wait for reset complete
    }

    // Software reset
    ESP_LOGI(TAG, "Sending Software Reset");
    spi_master_write_command(dev, 0x01);  // SWRESET: Software Reset
    delayMS(150);                        // Wait for reset process

    // Exit Sleep mode
    ESP_LOGI(TAG, "Exiting Sleep Mode");
    spi_master_write_command(dev, 0x11);  // SLPOUT: Sleep Out
    delayMS(120);

    // Memory Data Access Control
    ESP_LOGI(TAG, "Setting Memory Data Access Control");
    spi_master_write_command(dev, 0x36);  // MADCTL: Memory Data Access Control
    spi_master_write_data_byte(dev, 0x48); // Adjust orientation (0x48 for default landscape)
    delayMS(10);

    // Interface Pixel Format (16-bit/pixel)
    ESP_LOGI(TAG, "Setting Interface Pixel Format");
    spi_master_write_command(dev, 0x3A);  // COLMOD: Interface Pixel Format
    spi_master_write_data_byte(dev, 0x55); // 16-bit/pixel (RGB 5-6-5)
    delayMS(10);

    // Porch Setting
    ESP_LOGI(TAG, "Setting Porch Control");
    spi_master_write_command(dev, 0xB2);  // PORCTRL: Porch Control
    spi_master_write_data_byte(dev, 0x0C); // Front Porch
    spi_master_write_data_byte(dev, 0x0C); // Back Porch
    spi_master_write_data_byte(dev, 0x00); // Vertical Porch
    spi_master_write_data_byte(dev, 0x33);
    spi_master_write_data_byte(dev, 0x33);
    delayMS(10);

    // VCOM Setting
    ESP_LOGI(TAG, "Setting VCOM");
    spi_master_write_command(dev, 0xBB);  // VCOMS: VCOM Setting
    spi_master_write_data_byte(dev, 0x35); // VCOM voltage level (1.175V)
    delayMS(10);

    // LCM Control
    ESP_LOGI(TAG, "Setting LCM Control");
    spi_master_write_command(dev, 0xC0);  // LCMCTRL: LCM Control
    spi_master_write_data_byte(dev, 0x2C); // Default
    delayMS(10);

    // VDV and VRH Command Enable
    ESP_LOGI(TAG, "Enabling VDV and VRH Commands");
    spi_master_write_command(dev, 0xC2);  // VDVVRHEN: Enable VDV and VRH
    spi_master_write_data_byte(dev, 0x01); // Enable
    delayMS(10);

    // VRH Set
    ESP_LOGI(TAG, "Setting VRH");
    spi_master_write_command(dev, 0xC3);  // VRHS: VRH Set
    spi_master_write_data_byte(dev, 0x12); // Default
    delayMS(10);

    // VDV Set
    ESP_LOGI(TAG, "Setting VDV");
    spi_master_write_command(dev, 0xC4);  // VDVS: VDV Set
    spi_master_write_data_byte(dev, 0x20); // Default
    delayMS(10);

    // Frame Rate Control
    ESP_LOGI(TAG, "Setting Frame Rate Control");
    spi_master_write_command(dev, 0xC6);  // FRCTRL2: Frame Rate Control
    spi_master_write_data_byte(dev, 0x0F); // Default frame rate: 60Hz
    delayMS(10);

    // Power Control 1
    ESP_LOGI(TAG, "Setting Power Control 1");
    spi_master_write_command(dev, 0xD0);  // PWCTRL1: Power Control 1
    spi_master_write_data_byte(dev, 0xA4); // Default
    spi_master_write_data_byte(dev, 0xA1); // Default
    delayMS(10);

    // Positive Voltage Gamma Control
    ESP_LOGI(TAG, "Setting Positive Voltage Gamma Control");
    spi_master_write_command(dev, 0xE0);  // PGAMCTRL: Positive Gamma Control
    uint8_t positive_gamma[] = {0xD0, 0x08, 0x11, 0x08, 0x0C, 0x15, 0x39, 0x33, 0x50, 0x36, 0x13, 0x14, 0x29, 0x2D};
    for (int i = 0; i < sizeof(positive_gamma); i++) {
        spi_master_write_data_byte(dev, positive_gamma[i]);
    }
    delayMS(10);

    // Negative Voltage Gamma Control
    ESP_LOGI(TAG, "Setting Negative Voltage Gamma Control");
    spi_master_write_command(dev, 0xE1);  // NVGAMCTRL: Negative Gamma Control
    uint8_t negative_gamma[] = {0xD0, 0x08, 0x10, 0x08, 0x06, 0x06, 0x39, 0x44, 0x51, 0x0B, 0x16, 0x14, 0x2F, 0x31};
    for (int i = 0; i < sizeof(negative_gamma); i++) {
        spi_master_write_data_byte(dev, negative_gamma[i]);
    }
    delayMS(10);

    // Enable Display Inversion
    ESP_LOGI(TAG, "Enabling Display Inversion");
    spi_master_write_command(dev, 0x21);  // INVON: Display Inversion On
    delayMS(10);

    // Normal Display Mode On
    ESP_LOGI(TAG, "Setting Normal Display Mode");
    spi_master_write_command(dev, 0x13);  // NORON: Normal Display Mode On
    delayMS(10);

    // Turn on Display
    ESP_LOGI(TAG, "Turning Display On");
    spi_master_write_command(dev, 0x29);  // DISPON: Display On
    delayMS(120);                          // Wait for display to turn on

    // Backlight control
    if (dev->_bl >= 0) {
        gpio_set_level(dev->_bl, 1);
        ESP_LOGI(TAG, "Backlight turned on via lcdInit");
    }
}

// Draw pixel
// x:X coordinate
// y:Y coordinate
// color:color
void lcdDrawPixel(TFT_t *dev, uint16_t x, uint16_t y, uint16_t color) {
    if (x >= dev->_width)
        return;
    if (y >= dev->_height)
        return;

    uint16_t _x = x + dev->_offsetx;
    uint16_t _y = y + dev->_offsety;

    spi_master_write_command(dev, 0x2A);  // CASET: Column Address Set
    spi_master_write_addr(dev, _x, _x);
    spi_master_write_command(dev, 0x2B);  // RASET: Row Address Set
    spi_master_write_addr(dev, _y, _y);
    spi_master_write_command(dev, 0x2C);  // RAMWR: Memory Write
    spi_master_write_data_word(dev, color);
}

// Draw multi pixel
// x:X coordinate
// y:Y coordinate
// size:Number of colors
// colors:colors
void lcdDrawMultiPixels(TFT_t *dev, uint16_t x, uint16_t y, uint16_t size, uint16_t *colors) {
    if (x + size > dev->_width)
        return;
    if (y >= dev->_height)
        return;

    uint16_t _x1 = x + dev->_offsetx;
    uint16_t _x2 = _x1 + (size - 1);
    uint16_t _y1 = y + dev->_offsety;
    uint16_t _y2 = _y1;

    spi_master_write_command(dev, 0x2A);  // CASET: Column Address Set
    spi_master_write_addr(dev, _x1, _x2);
    spi_master_write_command(dev, 0x2B);  // RASET: Row Address Set
    spi_master_write_addr(dev, _y1, _y2);
    spi_master_write_command(dev, 0x2C);  // RAMWR: Memory Write
    spi_master_write_colors(dev, colors, size);
}

// Draw rectangle of filling
// x1:Start X coordinate
// y1:Start Y coordinate
// x2:End X coordinate
// y2:End Y coordinate
// color:color
void lcdDrawFillRect(TFT_t *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {
    if (x1 >= dev->_width)
        return;
    if (x2 >= dev->_width)
        x2 = dev->_width - 1;
    if (y1 >= dev->_height)
        return;
    if (y2 >= dev->_height)
        y2 = dev->_height - 1;

    uint16_t _x1 = x1 + dev->_offsetx;
    uint16_t _x2 = x2 + dev->_offsetx;
    uint16_t _y1 = y1 + dev->_offsety;
    uint16_t _y2 = y2 + dev->_offsety;

    spi_master_write_command(dev, 0x2A);  // CASET: Column Address Set
    spi_master_write_addr(dev, _x1, _x2);
    spi_master_write_command(dev, 0x2B);  // RASET: Row Address Set
    spi_master_write_addr(dev, _y1, _y2);
    spi_master_write_command(dev, 0x2C);  // RAMWR: Memory Write

    uint32_t size = (_x2 - _x1 + 1) * (_y2 - _y1 + 1);
    spi_master_write_color(dev, color, size);
}

// Display OFF
void lcdDisplayOff(TFT_t *dev) {
    spi_master_write_command(dev, 0x28);  // DISPOFF: Display Off
}

// Display ON
void lcdDisplayOn(TFT_t *dev) {
    spi_master_write_command(dev, 0x29);  // DISPON: Display On
}

// Fill screen
// color:color
void lcdFillScreen(TFT_t *dev, uint16_t color) {
    lcdDrawFillRect(dev, 0, 0, dev->_width - 1, dev->_height - 1, color);
}

// Draw line
// x1:Start X coordinate
// y1:Start Y coordinate
// x2:End   X coordinate
// y2:End   Y coordinate
// color:color
void lcdDrawLine(TFT_t *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {
    int dx, dy, sx, sy, err, e2;

    dx = abs(x2 - x1);
    dy = -abs(y2 - y1);

    sx = (x1 < x2) ? 1 : -1;
    sy = (y1 < y2) ? 1 : -1;

    err = dx + dy;

    while (1) {
        lcdDrawPixel(dev, x1, y1, color);
        if (x1 == x2 && y1 == y2)
            break;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

// Draw rectangle
// x1:Start X coordinate
// y1:Start Y coordinate
// x2:End   X coordinate
// y2:End   Y coordinate
// color:color
void lcdDrawRect(TFT_t *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color) {
    lcdDrawLine(dev, x1, y1, x2, y1, color);
    lcdDrawLine(dev, x2, y1, x2, y2, color);
    lcdDrawLine(dev, x2, y2, x1, y2, color);
    lcdDrawLine(dev, x1, y2, x1, y1, color);
}

// Draw rectangle with angle
// xc:Center X coordinate
// yc:Center Y coordinate
// w:Width of rectangle
// h:Height of rectangle
// angle:Angle of rectangle
// color:color

// When the origin is (0, 0), the point (x1, y1) after rotating the point (x, y) by the angle is obtained by the following calculation.
// x1 = x * cos(angle) - y * sin(angle)
// y1 = x * sin(angle) + y * cos(angle)
void lcdDrawRectAngle(TFT_t *dev, uint16_t xc, uint16_t yc, uint16_t w, uint16_t h, uint16_t angle, uint16_t color) {
    double xd, yd, rd;
    int x1, y1;
    int x2, y2;
    int x3, y3;
    int x4, y4;
    rd = -angle * M_PI / 180.0;
    xd = 0.0 - w / 2;
    yd = h / 2;
    x1 = (int)(xd * cos(rd) - yd * sin(rd) + xc);
    y1 = (int)(xd * sin(rd) + yd * cos(rd) + yc);

    yd = 0.0 - yd;
    x2 = (int)(xd * cos(rd) - yd * sin(rd) + xc);
    y2 = (int)(xd * sin(rd) + yd * cos(rd) + yc);

    xd = w / 2;
    yd = h / 2;
    x3 = (int)(xd * cos(rd) - yd * sin(rd) + xc);
    y3 = (int)(xd * sin(rd) + yd * cos(rd) + yc);

    yd = 0.0 - yd;
    x4 = (int)(xd * cos(rd) - yd * sin(rd) + xc);
    y4 = (int)(xd * sin(rd) + yd * cos(rd) + yc);

    lcdDrawLine(dev, x1, y1, x2, y2, color);
    lcdDrawLine(dev, x1, y1, x3, y3, color);
    lcdDrawLine(dev, x2, y2, x4, y4, color);
    lcdDrawLine(dev, x3, y3, x4, y4, color);
}

// Draw triangle
// xc:Center X coordinate
// yc:Center Y coordinate
// w:Width of triangle
// h:Height of triangle
// angle:Angle of triangle
// color:color

// When the origin is (0, 0), the point (x1, y1) after rotating the point (x, y) by the angle is obtained by the following calculation.
// x1 = x * cos(angle) - y * sin(angle)
// y1 = x * sin(angle) + y * cos(angle)
void lcdDrawTriangle(TFT_t *dev, uint16_t xc, uint16_t yc, uint16_t w, uint16_t h, uint16_t angle, uint16_t color) {
    double xd, yd, rd;
    int x1, y1;
    int x2, y2;
    int x3, y3;
    rd = -angle * M_PI / 180.0;
    xd = 0.0;
    yd = h / 2;
    x1 = (int)(xd * cos(rd) - yd * sin(rd) + xc);
    y1 = (int)(xd * sin(rd) + yd * cos(rd) + yc);

    xd = w / 2;
    yd = 0.0 - yd;
    x2 = (int)(xd * cos(rd) - yd * sin(rd) + xc);
    y2 = (int)(xd * sin(rd) + yd * cos(rd) + yc);

    xd = 0.0 - w / 2;
    x3 = (int)(xd * cos(rd) - yd * sin(rd) + xc);
    y3 = (int)(xd * sin(rd) + yd * cos(rd) + yc);

    lcdDrawLine(dev, x1, y1, x2, y2, color);
    lcdDrawLine(dev, x1, y1, x3, y3, color);
    lcdDrawLine(dev, x2, y2, x3, y3, color);
}

#if 0
// Draw UTF8 character
// x:X coordinate
// y:Y coordinate
// utf8:UTF8 code
// color:color
int lcdDrawUTF8Char(TFT_t * dev, FontxFile *fx, uint16_t x,uint16_t y,uint8_t *utf8,uint16_t color) {
    uint16_t sjis[1];

    sjis[0] = UTF2SJIS(utf8);
    if(_DEBUG_)printf("sjis=%04x\n",sjis[0]);
    return lcdDrawSJISChar(dev, fx, x, y, sjis[0], color);
}

// Draw UTF8 string
// x:X coordinate
// y:Y coordinate
// utfs:UTF8 string
// color:color
int lcdDrawUTF8String(TFT_t * dev, FontxFile *fx, uint16_t x, uint16_t y, unsigned char *utfs, uint16_t color) {

    int i;
    int spos;
    uint16_t sjis[64];
    spos = String2SJIS(utfs, strlen((char *)utfs), sjis, 64);
    if(_DEBUG_)printf("spos=%d\n",spos);
    ESP_LOGD(TAG, "Drawing UTF8 string with %d characters", spos);
    for(i=0;i<spos;i++) {
        if(_DEBUG_)printf("sjis[%d]=%x y=%d\n",i,sjis[i],y);
        if (dev->_font_direction == 0)
            x = lcdDrawSJISChar(dev, fx, x, y, sjis[i], color);
        if (dev->_font_direction == 1)
            y = lcdDrawSJISChar(dev, fx, x, y, sjis[i], color);
        if (dev->_font_direction == 2)
            x = lcdDrawSJISChar(dev, fx, x, y, sjis[i], color);
        if (dev->_font_direction == 3)
            y = lcdDrawSJISChar(dev, fx, x, y, sjis[i], color);
    }
    if (dev->_font_direction == 0) return x;
    if (dev->_font_direction == 2) return x;
    if (dev->_font_direction == 1) return y;
    if (dev->_font_direction == 3) return y;
    return 0;
}
#endif

// Set font direction
// dir:Direction
void lcdSetFontDirection(TFT_t *dev, uint16_t dir) {
    dev->_font_direction = dir;
}

// Set font filling
// color:fill color
void lcdSetFontFill(TFT_t *dev, uint16_t color) {
    dev->_font_fill = true;
    dev->_font_fill_color = color;
}

// UnSet font filling
void lcdUnsetFontFill(TFT_t *dev) {
    dev->_font_fill = false;
}

// Set font underline
// color:frame color
void lcdSetFontUnderLine(TFT_t *dev, uint16_t color) {
    dev->_font_underline = true;
    dev->_font_underline_color = color;
}

// UnSet font underline
void lcdUnsetFontUnderLine(TFT_t *dev) {
    dev->_font_underline = false;
}

// Backlight OFF
void lcdBacklightOff(TFT_t *dev) {
    if (dev->_bl >= 0) {
        gpio_set_level(dev->_bl, 0);
        ESP_LOGI(TAG, "Backlight turned off via lcdBacklightOff");
    }
}

// Backlight ON
void lcdBacklightOn(TFT_t *dev) {
    if (dev->_bl >= 0) {
        gpio_set_level(dev->_bl, 1);
        ESP_LOGI(TAG, "Backlight turned on via lcdBacklightOn");
    }
}

// Display Inversion Off
void lcdInversionOff(TFT_t *dev) {
    ESP_LOGI(TAG, "Disabling Display Inversion");
    spi_master_write_command(dev, 0x20);  // INVOFF: Display Inversion Off
}

// Display Inversion On
void lcdInversionOn(TFT_t *dev) {
    ESP_LOGI(TAG, "Enabling Display Inversion");
    spi_master_write_command(dev, 0x21);  // INVON: Display Inversion On
}