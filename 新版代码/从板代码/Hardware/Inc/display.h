/**
 * TFT显示与AI分类层 - 头文件
 * ILI9341 2.8寸 SPI TFT, 显示截面声速热力图和异常分类
 */
#ifndef __DISPLAY_H
#define __DISPLAY_H

#include "config.h"
#include "tomography.h"

// TFT分辨率
#define TFT_WIDTH   240
#define TFT_HEIGHT  320

// 颜色定义 (RGB565)
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_GRAY    0x7BEF
#define COLOR_ORANGE  0xFD20

class Display {
public:
    Display();
    
    // 初始化TFT
    void begin();
    
    // 显示截面声速热力图
    void drawVelocityMap(float* velocityMap, float vmin, float vmax);
    
    // 显示异常分类图
    void drawAnomalyMap(AnomalyType* anomalyMap, AnomalyType primary, 
                        float confidence, uint32_t scanTime_ms);
    
    // 显示虫害监听波形
    void drawWaveform(uint16_t* data, uint16_t count, uint32_t duration_ms);
    
    // 显示料位
    void drawGrainLevel(float level_mm, float siloHeight_mm);
    
    // 显示菜单
    void drawMenu(WorkMode currentMode);
    
    // 显示文字
    void drawText(uint16_t x, uint16_t y, const char* text, uint16_t color, uint8_t size);
    
    // 清屏
    void clear(uint16_t color = COLOR_BLACK);
    
private:
    // 软件SPI
    void spiWrite(uint8_t data);
    void spiWrite16(uint16_t data);
    
    // TFT命令/数据
    void writeCommand(uint8_t cmd);
    void writeData(uint8_t data);
    void writeData16(uint16_t data);
    
    // 设置窗口
    void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    
    // 画点/线/矩形
    void drawPixel(uint16_t x, uint16_t y, uint16_t color);
    void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    
    // 声速→颜色映射
    uint16_t velocityToColor(float v, float vmin, float vmax);
    
    // 异常类型→颜色映射
    uint16_t anomalyToColor(AnomalyType type);
    
    // 异常类型→文字
    const char* anomalyToString(AnomalyType type);
    
    // 字符绘制 (简易8x16字体)
    void drawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint8_t size);
};

#endif // __DISPLAY_H
