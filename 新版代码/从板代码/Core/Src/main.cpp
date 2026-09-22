/**
 * 粮仓声学层析成像系统 - 主程序
 * MCU: STM32F103C8T6
 *
 * 工作模式:
 *   1. 层析成像: 8路换能器逐对扫描 → ART重建 → 截面热力图
 *   2. 声发射监听: 被动采样 → 虫害声识别
 *   3. 料位测量: 顶部换能器反射测距
 *
 * 操作:
 *   PC13(START) → 启动扫描/测量
 *   PC14(MODE)  → 切换模式
 *   PC15(SAVE)  → 保存数据到Flash
 */

#include "config.h"
#include "tomography.h"
#include "sensor.h"
#include "display.h"

// 全局实例
Sensor sensor;
Tomography tomography;
Display display;

// 工作状态
static WorkMode currentMode = MODE_TOMOGRAPHY;
static bool isScanning = false;

// 测量数据
static RayMeasurement rayData[NUM_RAYS];
static ScanResult scanResult;

// 按键状态
static bool btnStartPressed = false;
static bool btnModePressed = false;

// ============ 按键读取 ============
void readButtons() {
    static uint32_t lastDebounce = 0;
    static bool lastStart = true;
    static bool lastMode = true;
    static bool lastSave = true;
    
    if (HAL_GetTick() - lastDebounce < 50) return;
    lastDebounce = HAL_GetTick();
    
    // START键
    bool start = HAL_GPIO_ReadPin(BTN_START_PORT, BTN_START_PIN);
    if (start == GPIO_PIN_RESET && lastStart == GPIO_PIN_SET) {
        btnStartPressed = true;
    }
    lastStart = start;
    
    // MODE键
    bool mode = HAL_GPIO_ReadPin(BTN_MODE_PORT, BTN_MODE_PIN);
    if (mode == GPIO_PIN_RESET && lastMode == GPIO_PIN_SET) {
        btnModePressed = true;
    }
    lastMode = mode;
    
    // SAVE键 (保存功能)
    bool save = HAL_GPIO_ReadPin(BTN_SAVE_PORT, BTN_SAVE_PIN);
    if (save == GPIO_PIN_RESET && lastSave == GPIO_PIN_SET) {
        // TODO: 保存当前结果到Flash/W25Q64
    }
    lastSave = save;
}

// ============ 层析成像模式 ============
void runTomography() {
    isScanning = true;
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
    
    display.clear(COLOR_BLACK);
    display.drawText(30, 10, "SCANNING...", COLOR_YELLOW, 2);
    
    uint32_t startTime = HAL_GetTick();
    
    // 1. 扫描全部28条射线
    sensor.scanAllRays(rayData);
    
    // 2. 提取穿越时间
    float rayTimes[NUM_RAYS];
    for (int i = 0; i < NUM_RAYS; i++) {
        if (rayData[i].valid) {
            rayTimes[i] = rayData[i].timeOfFlight_ms;
        } else {
            // 无效数据用平均时间填充
            rayTimes[i] = 0.5f;  // 0.5ms 默认值
        }
    }
    
    // 3. ART重建
    tomography.reconstruct(rayTimes, scanResult.velocityMap);
    
    // 4. 异常分类
    tomography.classifyAnomalies(scanResult.velocityMap,
                                  scanResult.anomalyMap,
                                  &scanResult.primaryAnomaly,
                                  &scanResult.confidence);
    
    scanResult.scanTime_ms = HAL_GetTick() - startTime;
    
    // 5. 显示结果
    // 先显示声速热力图3秒
    float vmin = 999, vmax = 0;
    for (int i = 0; i < GRID_PIXELS; i++) {
        float cx = (i % GRID_SIZE) * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
        float cy = (i / GRID_SIZE) * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
        if (cx*cx + cy*cy <= 90.0f*90.0f) {
            if (scanResult.velocityMap[i] < vmin) vmin = scanResult.velocityMap[i];
            if (scanResult.velocityMap[i] > vmax) vmax = scanResult.velocityMap[i];
        }
    }
    if (vmax - vmin < 0.01f) { vmax = vmin + 0.1f; }
    
    display.clear(COLOR_BLACK);
    display.drawText(20, 10, "VELOCITY MAP", COLOR_CYAN, 2);
    display.drawVelocityMap(scanResult.velocityMap, vmin, vmax);
    
    HAL_Delay(3000);
    
    // 6. 显示异常分类图
    display.clear(COLOR_BLACK);
    display.drawText(15, 10, "ANOMALY MAP", COLOR_CYAN, 2);
    display.drawAnomalyMap(scanResult.anomalyMap, scanResult.primaryAnomaly,
                           scanResult.confidence, scanResult.scanTime_ms);
    
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    isScanning = false;
}

// ============ 声发射监听模式 ============
void runAcousticEmission() {
    isScanning = true;
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
    
    display.clear(COLOR_BLACK);
    display.drawText(10, 10, "LISTENING...", COLOR_YELLOW, 2);
    display.drawText(10, 40, "Insect detection", COLOR_WHITE, 1);
    
    uint16_t aeBuffer[512];
    uint32_t duration = 3000;  // 采集3秒
    
    sensor.listenAcousticEmission(aeBuffer, 512, duration);
    
    // 分析声发射信号
    // 计算能量和频率特征
    float energy = 0;
    uint16_t maxVal = 0;
    uint16_t zeroCrossings = 0;
    uint16_t prevVal = aeBuffer[0];
    
    for (int i = 0; i < 512; i++) {
        float dev = (float)aeBuffer[i] - 2048;
        energy += dev * dev;
        if (aeBuffer[i] > maxVal) maxVal = aeBuffer[i];
        if ((prevVal < 2048 && aeBuffer[i] >= 2048) || 
            (prevVal >= 2048 && aeBuffer[i] < 2048)) {
            zeroCrossings++;
        }
        prevVal = aeBuffer[i];
    }
    energy = sqrtf(energy / 512);
    float approxFreq = (float)zeroCrossings * 25000.0f / (2.0f * 512);  // 估算频率
    
    // 显示波形
    display.drawWaveform(aeBuffer, 512, duration);
    
    // 显示分析结果
    char buf[64];
    snprintf(buf, sizeof(buf), "ENERGY: %d", (int)energy);
    display.drawText(10, 230, buf, COLOR_WHITE, 1);
    
    snprintf(buf, sizeof(buf), "FREQ: %dHz", (int)approxFreq);
    display.drawText(10, 245, buf, COLOR_WHITE, 1);
    
    // 简单虫害判断
    if (energy > 200 && approxFreq > 1000 && approxFreq < 15000) {
        display.drawText(10, 265, "INSECT DETECTED!", COLOR_RED, 2);
    } else if (energy > 100) {
        display.drawText(10, 265, "MAYBE INSECT", COLOR_YELLOW, 2);
    } else {
        display.drawText(10, 265, "NO INSECT", COLOR_GREEN, 2);
    }
    
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    isScanning = false;
}

// ============ 料位测量模式 ============
void runGrainLevel() {
    isScanning = true;
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
    
    display.clear(COLOR_BLACK);
    display.drawText(10, 10, "MEASURING...", COLOR_YELLOW, 2);
    
    // 多次测量取平均
    float level = 0;
    for (int i = 0; i < 5; i++) {
        level += sensor.measureGrainLevel_mm();
        HAL_Delay(100);
    }
    level /= 5.0f;
    
    display.drawGrainLevel(level, 300.0f);  // 假设仓高300mm
    
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
    isScanning = false;
}

// ============ 主函数 ============
int main(void) {
    // HAL初始化
    HAL_Init();
    
    // 系统时钟配置 (72MHz, 外部8MHz晶振)
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLMUL = RCC_PLL_MUL9;  // 8MHz × 9 = 72MHz
    HAL_RCC_OscConfig(&osc);
    
    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | 
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;   // 36MHz
    clk.APB2CLKDivider = RCC_HCLK_DIV1;   // 72MHz
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
    
    // 初始化各模块
    sensor.begin();
    display.begin();
    tomography.begin();
    
    // 开机画面
    display.clear(COLOR_BLACK);
    display.drawText(20, 50, "GRAIN ACOUSTIC", COLOR_CYAN, 2);
    display.drawText(30, 80, "TOMOGRAPHY", COLOR_CYAN, 2);
    display.drawText(50, 120, "SYSTEM V1.0", COLOR_WHITE, 1);
    HAL_Delay(2000);
    
    // 显示菜单
    display.drawMenu(currentMode);
    
    // 主循环
    while (1) {
        readButtons();
        
        // 模式切换
        if (btnModePressed) {
            btnModePressed = false;
            currentMode = (WorkMode)((currentMode + 1) % MODE_COUNT);
            display.drawMenu(currentMode);
        }
        
        // 开始扫描
        if (btnStartPressed && !isScanning) {
            btnStartPressed = false;
            
            switch (currentMode) {
                case MODE_TOMOGRAPHY:
                    runTomography();
                    break;
                case MODE_ACOUSTIC_EMISSION:
                    runAcousticEmission();
                    break;
                case MODE_LEVEL:
                    runGrainLevel();
                    break;
                default:
                    break;
            }
            
            // 扫描完成后返回菜单
            HAL_Delay(5000);
            display.drawMenu(currentMode);
        }
        
        HAL_Delay(10);
    }
}

// ============ HAL Tick (SysTick) ============
extern "C" void SysTick_Handler(void) {
    HAL_IncTick();
}
