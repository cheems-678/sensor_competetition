/**
 * TFT显示与AI分类层 - 实现
 * ILI9341 2.8寸 SPI TFT, 显示截面声速热力图和异常分类
 *
 * 显示布局:
 *   ┌──────────────────────┐
 *   │  粮仓声学层析成像系统  │  标题栏
 *   ├──────────────────────┤
 *   │                      │
 *   │   ▓▓▓▓▓▓▓▓▓▓▓▓      │  截面热力图
 *   │   ▓▓▓▒▒▓▓▓▓▓▓▓      │  16x16像素放大显示
 *   │   ▓▓▓▓▓▓▓▓▓▓▓▓      │
 *   │                      │
 *   ├──────────────────────┤
 *   │ 模式: 层析成像        │  状态栏
 *   │ 异常: 空洞 87%        │
 *   │ 耗时: 3200ms          │
 *   └──────────────────────┘
 */

#include "display.h"

// 简易8x16 ASCII字体 (仅数字和字母)
static const uint8_t font8x16[96][16] = {
    // 空格到ASCII 0x7F的子集, 这里只列出常用字符
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x00,0xF8,0xF8,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,0x1F,0x00,0x00,0x00}, // '!'
    // ... 完整字体表需要补充, 此处省略以节省篇幅
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '.'
    {0x00,0x38,0x44,0x4C,0x54,0x54,0x4C,0x44,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '0'
    {0x00,0x10,0x30,0x10,0x10,0x10,0x10,0x10,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '1'
    {0x00,0x78,0x84,0x84,0x0C,0x30,0x60,0xC0,0xFC,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '2'
    {0x00,0x78,0x84,0x04,0x38,0x04,0x84,0x84,0x78,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '3'
    {0x00,0x08,0x18,0x28,0x48,0xFC,0x08,0x08,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '4'
    {0x00,0xFC,0x80,0x80,0xF8,0x04,0x84,0x84,0x78,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '5'
    {0x00,0x38,0x40,0x80,0xF8,0x84,0x84,0x84,0x78,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '6'
    {0x00,0xFC,0x04,0x08,0x10,0x10,0x20,0x20,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '7'
    {0x00,0x78,0x84,0x84,0x78,0x84,0x84,0x84,0x78,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '8'
    {0x00,0x78,0x84,0x84,0x7C,0x04,0x08,0x10,0x70,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // '9'
    // A-Z (大写)
    {0x00,0x10,0x28,0x44,0x44,0xFC,0x44,0x44,0x44,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 'A'
    {0x00,0xF8,0x44,0x44,0x78,0x44,0x44,0x44,0xF8,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 'B'
    {0x00,0x78,0x84,0x80,0x80,0x80,0x80,0x84,0x78,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 'C'
    {0x00,0xF0,0x48,0x44,0x44,0x44,0x44,0x48,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 'D'
    {0x00,0xFC,0x40,0x40,0x70,0x40,0x40,0x40,0xFC,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 'E'
    {0x00,0xFC,0x40,0x40,0x70,0x40,0x40,0x40,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 'F'
    {0x00,0x78,0x84,0x80,0x9C,0x84,0x84,0x84,0x78,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 'G'
    {0x00,0x44,0x44,0x44,0xFC,0x44,0x44,0x44,0x44,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 'H'
    {0x00,0x38,0x10,0x10,0x10,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 'I'
    // ... 其余字符省略
};

Display::Display() {}

void Display::begin() {
    // TFT RST
    HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(TFT_RST_PORT, TFT_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(100);
    
    // ILI9341初始化命令序列
    writeCommand(0x01);  // 软复位
    HAL_Delay(100);
    
    writeCommand(0xCB); writeData(0x39); writeData(0x2C); writeData(0x00); writeData(0x34); writeData(0x02);
    writeCommand(0xCF); writeData(0x00); writeData(0XC1); writeData(0X30);
    writeCommand(0xE8); writeData(0x85); writeData(0x00); writeData(0x78);
    writeCommand(0xEA); writeData(0x00); writeData(0x00);
    writeCommand(0xED); writeData(0x64); writeData(0x03); writeData(0X12); writeData(0X81);
    writeCommand(0xF7); writeData(0x20);
    writeCommand(0xC0); writeData(0x23);  // Power control VRH[5:0]
    writeCommand(0xC1); writeData(0x10);  // Power control SAP[2:0]
    writeCommand(0xC5); writeData(0x3E); writeData(0x28);  // VCOM
    writeCommand(0xC7); writeData(0x86);  // VCOM offset
    writeCommand(0x36); writeData(0x48);  // Memory access control (横屏)
    writeCommand(0x3A); writeData(0x55);  // Pixel format 16bit
    writeCommand(0xB1); writeData(0x00); writeData(0x18);  // Framerate
    writeCommand(0xB6); writeData(0x08); writeData(0x82); writeData(0x27);  // Display function
    writeCommand(0x26); writeData(0x01);  // Gamma
    writeCommand(0xE0);  // Gamma correction (+)
    writeData(0x0F); writeData(0x31); writeData(0x2B); writeData(0x0C); writeData(0x0E); writeData(0x08);
    writeData(0x4E); writeData(0xF1); writeData(0x37); writeData(0x07); writeData(0x10); writeData(0x03);
    writeData(0x0E); writeData(0x09); writeData(0x00);
    writeCommand(0xE1);  // Gamma correction (-)
    writeData(0x00); writeData(0x0E); writeData(0x14); writeData(0x03); writeData(0x11); writeData(0x07);
    writeData(0x31); writeData(0xC1); writeData(0x48); writeData(0x08); writeData(0x0F); writeData(0x0C);
    writeData(0x31); writeData(0x36); writeData(0x0F);
    
    writeCommand(0x11);  // 退出睡眠
    HAL_Delay(120);
    writeCommand(0x29);  // 开显示
    
    // 开背光
    HAL_GPIO_WritePin(TFT_BL_PORT, TFT_BL_PIN, GPIO_PIN_SET);
    
    clear(COLOR_BLACK);
}

void Display::spiWrite(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        HAL_GPIO_WritePin(TFT_SCK_PORT, TFT_SCK_PIN, GPIO_PIN_RESET);
        if (data & 0x80) 
            HAL_GPIO_WritePin(TFT_MOSI_PORT, TFT_MOSI_PIN, GPIO_PIN_SET);
        else 
            HAL_GPIO_WritePin(TFT_MOSI_PORT, TFT_MOSI_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(TFT_SCK_PORT, TFT_SCK_PIN, GPIO_PIN_SET);
        data <<= 1;
    }
}

void Display::spiWrite16(uint16_t data) {
    spiWrite(data >> 8);
    spiWrite(data & 0xFF);
}

void Display::writeCommand(uint8_t cmd) {
    HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_RESET);  // DC=0 命令
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_RESET);
    spiWrite(cmd);
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_SET);
}

void Display::writeData(uint8_t data) {
    HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_SET);    // DC=1 数据
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_RESET);
    spiWrite(data);
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_SET);
}

void Display::writeData16(uint16_t data) {
    HAL_GPIO_WritePin(TFT_DC_PORT, TFT_DC_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_RESET);
    spiWrite16(data);
    HAL_GPIO_WritePin(TFT_CS_PORT, TFT_CS_PIN, GPIO_PIN_SET);
}

void Display::setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    writeCommand(0x2A);  // 列地址
    writeData16(x0);
    writeData16(x1);
    writeCommand(0x2B);  // 行地址
    writeData16(y0);
    writeData16(y1);
    writeCommand(0x2C);  // 写GRAM
}

void Display::drawPixel(uint16_t x, uint16_t y, uint16_t color) {
    setWindow(x, y, x, y);
    writeData16(color);
}

void Display::fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    setWindow(x, y, x + w - 1, y + h - 1);
    for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
        writeData16(color);
    }
}

void Display::clear(uint16_t color) {
    fillRect(0, 0, TFT_WIDTH, TFT_HEIGHT, color);
}

uint16_t Display::velocityToColor(float v, float vmin, float vmax) {
    // 声速 → 颜色映射 (蓝→绿→黄→红)
    // 正常(中间值) = 绿色, 偏高(空洞) = 红色, 偏低(受潮) = 蓝色
    float range = vmax - vmin;
    if (range < 0.001f) return COLOR_GREEN;
    
    float norm = (v - vmin) / range;  // 0~1
    if (norm < 0) norm = 0;
    if (norm > 1) norm = 1;
    
    // HSL色带: 蓝(240°) → 绿(120°) → 红(0°)
    float hue = (1.0f - norm) * 240.0f;  // 0=红, 240=蓝
    float sat = 1.0f;
    float lit = 0.5f;
    
    // HSL → RGB565
    float c = (1.0f - fabsf(2.0f * lit - 1.0f)) * sat;
    float x = c * (1.0f - fabsf(fmodf(hue / 60.0f, 2.0f) - 1.0f));
    float m = lit - c / 2.0f;
    float r, g, b;
    
    if (hue < 60)        { r = c; g = x; b = 0; }
    else if (hue < 120)  { r = x; g = c; b = 0; }
    else if (hue < 180)  { r = 0; g = c; b = x; }
    else if (hue < 240)  { r = 0; g = x; b = c; }
    else                 { r = x; g = 0; b = c; }
    
    uint8_t r5 = (uint8_t)((r + m) * 31);
    uint8_t g6 = (uint8_t)((g + m) * 63);
    uint8_t b5 = (uint8_t)((b + m) * 31);
    
    return (r5 << 11) | (g6 << 5) | b5;
}

uint16_t Display::anomalyToColor(AnomalyType type) {
    switch (type) {
        case ANOMALY_NONE:       return COLOR_GREEN;
        case ANOMALY_VOID:       return COLOR_RED;      // 空洞=红
        case ANOMALY_WET:        return COLOR_BLUE;     // 受潮=蓝
        case ANOMALY_HOT:        return COLOR_ORANGE;   // 发热=橙
        case ANOMALY_COMPACTED:  return COLOR_YELLOW;   // 压实=黄
        case ANOMALY_INSECT:     return COLOR_MAGENTA;  // 虫害=品红
        default:                 return COLOR_GRAY;
    }
}

const char* Display::anomalyToString(AnomalyType type) {
    switch (type) {
        case ANOMALY_NONE:      return "NORMAL";
        case ANOMALY_VOID:      return "VOID";
        case ANOMALY_WET:       return "WET";
        case ANOMALY_HOT:       return "HOT";
        case ANOMALY_COMPACTED: return "COMPACT";
        case ANOMALY_INSECT:    return "INSECT";
        default:                return "UNKNOWN";
    }
}

void Display::drawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint8_t size) {
    if (c < 32 || c > 127) c = '?';
    const uint8_t* glyph = font8x16[c - 32];
    
    for (uint8_t row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (uint8_t col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                if (size == 1) {
                    drawPixel(x + col, y + row, color);
                } else {
                    fillRect(x + col * size, y + row * size, size, size, color);
                }
            }
        }
    }
}

void Display::drawText(uint16_t x, uint16_t y, const char* text, uint16_t color, uint8_t size) {
    uint16_t cx = x;
    while (*text) {
        drawChar(cx, y, *text, color, size);
        cx += 8 * size + 1;
        text++;
    }
}

void Display::drawVelocityMap(float* velocityMap, float vmin, float vmax) {
    // 16x16像素截面图, 每像素12x12 = 192x192
    // 居中显示在屏幕中央
    uint16_t startX = (TFT_WIDTH - 16 * 12) / 2;  // 24
    uint16_t startY = 40;
    
    for (int row = 0; row < GRID_SIZE; row++) {
        for (int col = 0; col < GRID_SIZE; col++) {
            float v = velocityMap[row * GRID_SIZE + col];
            
            // 排除圆筒外区域 (角落)
            float cx = col * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
            float cy = row * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
            
            if (cx*cx + cy*cy > 100.0f*100.0f) {
                // 圆筒外, 画深灰
                fillRect(startX + col * 12, startY + row * 12, 12, 12, COLOR_GRAY);
            } else {
                uint16_t color = velocityToColor(v, vmin, vmax);
                fillRect(startX + col * 12, startY + row * 12, 12, 12, color);
            }
        }
    }
    
    // 画色标条
    uint16_t barX = startX + 16 * 12 + 10;
    for (int i = 0; i < 100; i++) {
        uint16_t color = velocityToColor(vmin + (vmax - vmin) * i / 100, vmin, vmax);
        fillRect(barX, startY + 100 - i, 12, 1, color);
    }
    drawText(barX + 15, startY, "HIGH", COLOR_WHITE, 1);
    drawText(barX + 15, startY + 90, "LOW", COLOR_WHITE, 1);
}

void Display::drawAnomalyMap(AnomalyType* anomalyMap, AnomalyType primary,
                             float confidence, uint32_t scanTime_ms) {
    uint16_t startX = (TFT_WIDTH - 16 * 12) / 2;
    uint16_t startY = 40;
    
    for (int row = 0; row < GRID_SIZE; row++) {
        for (int col = 0; col < GRID_SIZE; col++) {
            AnomalyType a = anomalyMap[row * GRID_SIZE + col];
            float cx = col * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
            float cy = row * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
            
            if (cx*cx + cy*cy > 100.0f*100.0f) {
                fillRect(startX + col * 12, startY + row * 12, 12, 12, COLOR_GRAY);
            } else {
                fillRect(startX + col * 12, startY + row * 12, 12, 12, anomalyToColor(a));
            }
        }
    }
    
    // 状态栏
    uint16_t barY = startY + 16 * 12 + 10;
    fillRect(0, barY, TFT_WIDTH, TFT_HEIGHT - barY, COLOR_BLACK);
    
    char buf[64];
    snprintf(buf, sizeof(buf), "ANOMALY: %s", anomalyToString(primary));
    drawText(10, barY, buf, anomalyToColor(primary), 2);
    
    snprintf(buf, sizeof(buf), "CONF: %d%%", (int)(confidence * 100));
    drawText(10, barY + 30, buf, COLOR_WHITE, 2);
    
    snprintf(buf, sizeof(buf), "TIME: %dms", (int)scanTime_ms);
    drawText(10, barY + 60, buf, COLOR_CYAN, 1);
}

void Display::drawWaveform(uint16_t* data, uint16_t count, uint32_t duration_ms) {
    clear(COLOR_BLACK);
    drawText(10, 10, "ACOUSTIC EMISSION", COLOR_CYAN, 2);
    
    // 画波形
    uint16_t waveY = 50;
    uint16_t waveH = 150;
    
    fillRect(0, waveY, TFT_WIDTH, waveH, COLOR_BLACK);
    
    // 画网格线
    for (int x = 0; x < TFT_WIDTH; x += 20) {
        for (int y = 0; y < waveH; y += 20) {
            drawPixel(x, waveY + y, COLOR_GRAY);
        }
    }
    
    // 画波形
    uint16_t step = (count > TFT_WIDTH) ? count / TFT_WIDTH : 1;
    uint16_t px = 0;
    for (uint16_t i = 0; i < count && px < TFT_WIDTH; i += step) {
        uint16_t val = data[i];
        uint16_t y = waveY + waveH - (val * waveH / 4096);
        if (y < waveY) y = waveY;
        if (y >= waveY + waveH) y = waveY + waveH - 1;
        drawPixel(px, y, COLOR_GREEN);
        px++;
    }
    
    // 显示统计信息
    char buf[64];
    snprintf(buf, sizeof(buf), "DURATION: %dms", (int)duration_ms);
    drawText(10, waveY + waveH + 10, buf, COLOR_WHITE, 1);
}

void Display::drawGrainLevel(float level_mm, float siloHeight_mm) {
    clear(COLOR_BLACK);
    drawText(10, 10, "GRAIN LEVEL", COLOR_CYAN, 2);
    
    // 画粮仓示意
    uint16_t siloX = 80;
    uint16_t siloY = 50;
    uint16_t siloW = 80;
    uint16_t siloH = 200;
    
    fillRect(siloX, siloY, siloW, siloH, COLOR_GRAY);
    
    // 画粮面
    float ratio = (siloHeight_mm > 0) ? level_mm / siloHeight_mm : 0;
    if (ratio > 1.0f) ratio = 1.0f;
    uint16_t fillH = (uint16_t)(siloH * ratio);
    fillRect(siloX, siloY + siloH - fillH, siloW, fillH, COLOR_YELLOW);
    
    // 显示数值
    char buf[64];
    snprintf(buf, sizeof(buf), "LEVEL: %dmm", (int)level_mm);
    drawText(10, siloY + siloH + 10, buf, COLOR_WHITE, 2);
    
    snprintf(buf, sizeof(buf), "TOTAL: %dmm", (int)siloHeight_mm);
    drawText(10, siloY + siloH + 40, buf, COLOR_WHITE, 1);
}

void Display::drawMenu(WorkMode currentMode) {
    clear(COLOR_BLACK);
    drawText(30, 10, "GRAIN ACOUSTIC CT", COLOR_CYAN, 2);
    
    const char* modes[] = {
        "1. TOMOGRAPHY",
        "2. INSECT LISTEN",
        "3. GRAIN LEVEL"
    };
    
    for (int i = 0; i < MODE_COUNT; i++) {
        uint16_t color = (i == currentMode) ? COLOR_YELLOW : COLOR_WHITE;
        drawText(30, 60 + i * 30, modes[i], color, 2);
    }
    
    drawText(30, 180, "START: SCAN", COLOR_GREEN, 1);
    drawText(30, 200, "MODE: SWITCH", COLOR_GREEN, 1);
}
