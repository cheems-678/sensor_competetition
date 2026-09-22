/**
 * 声学层析成像核心算法 - 头文件
 * ART(代数重建技术) + 射线路径追踪 + 异常分类
 */
#ifndef __TOMOGRAPHY_H
#define __TOMOGRAPHY_H

#include "config.h"

// 射线路径: 记录每条射线经过的像素索引和长度
typedef struct {
    uint8_t pixelIdx[64];   // 经过的像素索引(最多64个)
    float pixelLen[64];     // 在每个像素中的路径长度(mm)
    uint8_t numPixels;      // 经过的像素数
    uint8_t txIdx;          // 发射换能器索引
    uint8_t rxIdx;          // 接收换能器索引
} RayPath;

class Tomography {
public:
    Tomography();
    
    // 初始化: 计算所有射线路径
    void begin();
    
    // ART重建: 输入28条射线的穿越时间, 输出16x16声速分布图
    void reconstruct(float* rayTimes_ms, float* velocityMap);
    
    // 异常检测: 根据声速分布分类异常类型
    void classifyAnomalies(float* velocityMap, AnomalyType* anomalyMap, 
                           AnomalyType* primaryAnomaly, float* confidence);
    
    // 获取射线路径(调试用)
    RayPath* getRayPaths() { return m_rays; }
    
private:
    RayPath m_rays[NUM_RAYS];     // 28条射线路径
    float m_rayMatrix[NUM_RAYS][GRID_PIXELS];  // 稀疏射线矩阵
    
    // 换能器坐标(mm), 圆筒直径200mm
    static const float TX_X[NUM_TRANSDUCERS];
    static const float TX_Y[NUM_TRANSDUCERS];
    
    // 射线追踪: 计算一条射线经过的像素和长度
    void traceRay(uint8_t txIdx, uint8_t rxIdx, RayPath* path);
    
    // 线段与网格求交
    bool lineGridIntersection(float x1, float y1, float x2, float y2,
                               uint8_t* indices, float* lengths, uint8_t* count);
    
    // ART迭代
    void artIterate(float* rayTimes, float* velocityMap);
};

#endif // __TOMOGRAPHY_H
