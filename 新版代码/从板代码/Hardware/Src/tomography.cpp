/**
 * 声学层析成像核心算法 - 实现
 * ART(代数重建技术) + 射线路径追踪 + 异常分类
 *
 * 原理:
 * 1. 8个压电换能器等分圆形排列, 两两组合形成28条射线
 * 2. 测量每条射线的超声波穿越时间
 * 3. 用ART算法反解256个像素的声速值
 * 4. 声速异常区域 → 分类为空洞/受潮/发热/虫害
 */

#include "tomography.h"

// 换能器坐标: 圆筒内壁半径100mm, 8个换能器等分
const float Tomography::TX_X[8] = {
    100.0f, 70.71f, 0.0f, -70.71f, -100.0f, -70.71f, 0.0f, 70.71f
};
const float Tomography::TX_Y[8] = {
    0.0f, 70.71f, 100.0f, 70.71f, 0.0f, -70.71f, -100.0f, -70.71f
};

Tomography::Tomography() {
    memset(m_rays, 0, sizeof(m_rays));
    memset(m_rayMatrix, 0, sizeof(m_rayMatrix));
}

void Tomography::begin() {
    // 计算所有28条射线路径
    int rayIdx = 0;
    for (int i = 0; i < NUM_TRANSDUCERS; i++) {
        for (int j = i + 1; j < NUM_TRANSDUCERS; j++) {
            m_rays[rayIdx].txIdx = i;
            m_rays[rayIdx].rxIdx = j;
            traceRay(i, j, &m_rays[rayIdx]);
            
            // 填充稀疏射线矩阵
            for (int k = 0; k < m_rays[rayIdx].numPixels; k++) {
                uint8_t idx = m_rays[rayIdx].pixelIdx[k];
                m_rayMatrix[rayIdx][idx] = m_rays[rayIdx].pixelLen[k];
            }
            rayIdx++;
        }
    }
}

void Tomography::traceRay(uint8_t txIdx, uint8_t rxIdx, RayPath* path) {
    float x1 = TX_X[txIdx];
    float y1 = TX_Y[txIdx];
    float x2 = TX_X[rxIdx];
    float y2 = TX_Y[rxIdx];
    
    // 使用Siddon算法计算射线与网格交点
    uint8_t indices[64];
    float lengths[64];
    uint8_t count = 0;
    
    lineGridIntersection(x1, y1, x2, y2, indices, lengths, &count);
    
    path->numPixels = (count > 64) ? 64 : count;
    for (uint8_t i = 0; i < path->numPixels; i++) {
        path->pixelIdx[i] = indices[i];
        path->pixelLen[i] = lengths[i];
    }
}

bool Tomography::lineGridIntersection(float x1, float y1, float x2, float y2,
                                       uint8_t* indices, float* lengths, uint8_t* count) {
    // 坐标变换到网格坐标系: [-100,100]mm → [0,15]
    float gx1 = (x1 + 100.0f) / PIXEL_SIZE_MM;
    float gy1 = (y1 + 100.0f) / PIXEL_SIZE_MM;
    float gx2 = (x2 + 100.0f) / PIXEL_SIZE_MM;
    float gy2 = (y2 + 100.0f) / PIXEL_SIZE_MM;
    
    // Bresenham-like 网格遍历 (简化版数字微分分析法DDA)
    float dx = gx2 - gx1;
    float dy = gy2 - gy1;
    float dist = sqrtf(dx*dx + dy*dy);
    if (dist < 0.001f) { *count = 0; return false; }
    
    float stepX = dx / dist;
    float stepY = dy / dist;
    
    uint8_t cnt = 0;
    float cx = gx1;
    float cy = gy1;
    
    // 沿射线逐步遍历
    float stepLen = 0.5f;  // 步长0.5个像素
    float totalLen = 0.0f;
    
    // 先记录起始像素
    int lastPx = (int)cx;
    int lastPy = (int)cy;
    lastPx = (lastPx < 0) ? 0 : (lastPx >= GRID_SIZE ? GRID_SIZE-1 : lastPx);
    lastPy = (lastPy < 0) ? 0 : (lastPy >= GRID_SIZE ? GRID_SIZE-1 : lastPy);
    uint8_t lastIdx = lastPy * GRID_SIZE + lastPx;
    float pixelAccumLen = 0.0f;
    
    for (float t = 0; t <= dist; t += stepLen) {
        cx = gx1 + stepX * t;
        cy = gy1 + stepY * t;
        
        int px = (int)cx;
        int py = (int)cy;
        px = (px < 0) ? 0 : (px >= GRID_SIZE ? GRID_SIZE-1 : px);
        py = (py < 0) ? 0 : (py >= GRID_SIZE ? GRID_SIZE-1 : py);
        uint8_t idx = py * GRID_SIZE + px;
        
        if (idx == lastIdx) {
            // 仍在同一像素内, 累计长度
            pixelAccumLen += stepLen * PIXEL_SIZE_MM;
        } else {
            // 进入新像素, 保存上一个
            indices[cnt] = lastIdx;
            lengths[cnt] = pixelAccumLen;
            cnt++;
            if (cnt >= 64) break;
            pixelAccumLen = stepLen * PIXEL_SIZE_MM;
            lastIdx = idx;
        }
        totalLen += stepLen;
    }
    // 保存最后一个像素
    if (cnt < 64 && pixelAccumLen > 0) {
        indices[cnt] = lastIdx;
        lengths[cnt] = pixelAccumLen;
        cnt++;
    }
    
    *count = cnt;
    return cnt > 0;
}

void Tomography::reconstruct(float* rayTimes_ms, float* velocityMap) {
    // 初始化声速图为均匀分布(粮食平均声速)
    for (int i = 0; i < GRID_PIXELS; i++) {
        velocityMap[i] = SOUND_GRAIN_MS;  // 0.30 m/ms
    }
    
    // ART迭代重建
    artIterate(rayTimes_ms, velocityMap);
}

void Tomography::artIterate(float* rayTimes, float* velocityMap) {
    // ART算法:
    // 对每条射线r, 计算当前模型的预测时间: T_pred = sum(L_ij * (1/v_j))
    // 计算误差: dT = T_meas - T_pred
    // 更新: v_j += lambda * dT * L_ij / sum(L_ij^2)
    //
    // 其中 L_ij = 射线i在像素j中的长度
    //      v_j  = 像素j的声速
    //      T    = 穿越 = sum(L_ij / v_j)
    
    for (int iter = 0; iter < ART_ITERATIONS; iter++) {
        for (int r = 0; r < NUM_RAYS; r++) {
            // 计算预测穿越时间
            float tPred = 0.0f;
            float sumL2 = 0.0f;
            
            for (int p = 0; p < m_rays[r].numPixels; p++) {
                uint8_t idx = m_rays[r].pixelIdx[p];
                float len = m_rays[r].pixelLen[p];
                if (velocityMap[idx] > 0.001f) {
                    tPred += len / velocityMap[idx];  // len(mm) / v(m/ms) = ms
                }
                sumL2 += len * len;
            }
            
            if (sumL2 < 0.001f) continue;
            
            // 误差
            float dT = rayTimes[r] - tPred;
            
            // 更新每个像素的声速
            for (int p = 0; p < m_rays[r].numPixels; p++) {
                uint8_t idx = m_rays[r].pixelIdx[p];
                float len = m_rays[r].pixelLen[p];
                
                // 声慢度 (声速的倒数) 更新
                float slowness = 1.0f / velocityMap[idx];
                slowness += ART_RELAXATION * dT * len / sumL2;
                
                // 声慢度 → 声速
                if (slowness > 0.001f) {
                    velocityMap[idx] = 1.0f / slowness;
                }
                
                // 约束
                if (velocityMap[idx] < ART_MIN_VALUE) 
                    velocityMap[idx] = ART_MIN_VALUE;
                if (velocityMap[idx] > ART_MAX_VALUE) 
                    velocityMap[idx] = ART_MAX_VALUE;
            }
        }
    }
}

void Tomography::classifyAnomalies(float* velocityMap, AnomalyType* anomalyMap,
                                    AnomalyType* primaryAnomaly, float* confidence) {
    // 统计正常声速范围
    float sum = 0, count = 0;
    for (int i = 0; i < GRID_PIXELS; i++) {
        // 只统计圆筒内部的像素 (排除角落)
        float cx = (i % GRID_SIZE) * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
        float cy = (i / GRID_SIZE) * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
        if (cx*cx + cy*cy <= 90.0f*90.0f) {
            sum += velocityMap[i];
            count++;
        }
    }
    float avgVel = (count > 0) ? sum / count : SOUND_GRAIN_MS;
    float stdVel = 0;
    for (int i = 0; i < GRID_PIXELS; i++) {
        float cx = (i % GRID_SIZE) * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
        float cy = (i / GRID_SIZE) * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
        if (cx*cx + cy*cy <= 90.0f*90.0f) {
            stdVel += (velocityMap[i] - avgVel) * (velocityMap[i] - avgVel);
        }
    }
    stdVel = (count > 0) ? sqrtf(stdVel / count) : 0.0f;
    
    // 分类每个像素
    int anomalyCount[ANOMALY_COUNT] = {0};
    for (int i = 0; i < GRID_PIXELS; i++) {
        float cx = (i % GRID_SIZE) * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
        float cy = (i / GRID_SIZE) * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
        if (cx*cx + cy*cy > 90.0f*90.0f) {
            anomalyMap[i] = ANOMALY_NONE;
            continue;
        }
        
        float v = velocityMap[i];
        float dev = v - avgVel;
        
        if (dev > stdVel * 1.5f) {
            // 声速偏高 → 空洞(空气声速比粮食快)
            anomalyMap[i] = ANOMALY_VOID;
            anomalyCount[ANOMALY_VOID]++;
        } else if (dev < -stdVel * 2.0f) {
            // 声速偏低 → 受潮(水分增加声速下降)
            anomalyMap[i] = ANOMALY_WET;
            anomalyCount[ANOMALY_WET]++;
        } else if (dev < -stdVel * 1.0f && dev > -stdVel * 2.0f) {
            // 轻度偏低 → 压实(密度增加声速略降)
            anomalyMap[i] = ANOMALY_COMPACTED;
            anomalyCount[ANOMALY_COMPACTED]++;
        } else {
            anomalyMap[i] = ANOMALY_NONE;
            anomalyCount[ANOMALY_NONE]++;
        }
    }
    
    // 找主要异常
    int maxCount = 0;
    *primaryAnomaly = ANOMALY_NONE;
    for (int i = 1; i < ANOMALY_COUNT; i++) {
        if (anomalyCount[i] > maxCount) {
            maxCount = anomalyCount[i];
            *primaryAnomaly = (AnomalyType)i;
        }
    }
    
    // 置信度: 异常像素占比 × (1 - 噪声比)
    int totalPixels = 0;
    for (int i = 0; i < GRID_PIXELS; i++) {
        float cx = (i % GRID_SIZE) * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
        float cy = (i / GRID_SIZE) * PIXEL_SIZE_MM - 100.0f + PIXEL_SIZE_MM/2;
        if (cx*cx + cy*cy <= 90.0f*90.0f) totalPixels++;
    }
    *confidence = (totalPixels > 0) ? 
        (float)maxCount / totalPixels : 0.0f;
    if (*confidence > 1.0f) *confidence = 1.0f;
}
