/**
 * ART代数重建层析算法 - 实现
 *
 * 8个换能器坐标(mm, 中心在原点):
 *   T0(100,0)  T1(70.7,70.7)  T2(0,100)    T3(-70.7,70.7)
 *   T4(-100,0) T5(-70.7,-70.7) T6(0,-100)   T7(70.7,-70.7)
 *
 * 网格: [-100,100]mm → 16×16, 每格12.5mm
 */

#include "tomography.h"

/* ========= 换能器坐标 (mm) ========= */
static const float TX_X[8] = {
    100.0f, 70.71f, 0.0f, -70.71f, -100.0f, -70.71f, 0.0f, 70.71f
};
static const float TX_Y[8] = {
    0.0f, 70.71f, 100.0f, 70.71f, 0.0f, -70.71f, -100.0f, -70.71f
};

/* ========= 内部存储 =========
 * 注意: 由于STM32F103C8只有20KB RAM, 不存完整的[28][256]稠密矩阵(28.6KB)!
 * 直接用 RayPath 里的 pix[]/len[] 稀疏数组, 每条平均~20个像素 = 28×(64+64+2) = 3.7KB */
static RayPath rays[NUM_RAYS];

/* ========= 射线追踪: Siddon简化版 ========= */
static void traceRay(uint8_t txi, uint8_t rxi, RayPath* p) {
    /* 真实坐标(mm) -> 网格坐标[0,16) */
    float x1 = (TX_X[txi] + 100.0f) / PIXEL_MM;
    float y1 = (TX_Y[txi] + 100.0f) / PIXEL_MM;
    float x2 = (TX_X[rxi] + 100.0f) / PIXEL_MM;
    float y2 = (TX_Y[rxi] + 100.0f) / PIXEL_MM;
    float dx = x2 - x1, dy = y2 - y1;
    float d  = sqrtf(dx*dx + dy*dy);
    if (d < 0.001f) { p->n = 0; return; }

    float sx = dx / d, sy = dy / d;   /* 单位步长 */
    uint8_t cnt = 0;
    int     lastPx = -1, lastPy = -1;
    float   accum = 0;

    for (float t = 0; t <= d; t += 0.5f) {  /* 步长0.5格 */
        float cx = x1 + sx * t;
        float cy = y1 + sy * t;
        int px = (int)cx, py = (int)cy;
        if (px < 0) px = 0; if (px >= GRID_N) px = GRID_N-1;
        if (py < 0) py = 0; if (py >= GRID_N) py = GRID_N-1;
        uint8_t idx = (uint8_t)(py * GRID_N + px);

        if ((px == lastPx) && (py == lastPy)) {
            accum += 0.5f * PIXEL_MM;
        } else {
            if (lastPx >= 0 && cnt < 64) {
                p->pix[cnt]   = (uint8_t)(lastPy * GRID_N + lastPx);
                p->len[cnt++] = accum;
            }
            accum = 0.5f * PIXEL_MM;
            lastPx = px; lastPy = py;
        }
    }
    if (cnt < 64 && lastPx >= 0) {
        p->pix[cnt]   = (uint8_t)(lastPy * GRID_N + lastPx);
        p->len[cnt++] = accum;
    }
    p->n  = cnt;
    p->tx = txi;
    p->rx = rxi;
}

/* ========= 初始化 ========= */
void Tomo_Init(void) {
    memset(rays, 0, sizeof(rays));
    int r = 0;
    for (int i = 0; i < NUM_TX; i++) {
        for (int j = i+1; j < NUM_TX; j++) {
            traceRay(i, j, &rays[r]);
            r++;
        }
    }
}

RayPath* Tomo_GetRays(void) { return rays; }

/* ========= ART重建 ========= */
void Tomo_Reconstruct(float* rayT, float* vmap) {
    /* 初始化均匀声速 */
    for (int i = 0; i < GRID_PIX; i++) vmap[i] = V_GRAIN_MS;

    /* 迭代 */
    for (int it = 0; it < ART_ITER; it++) {
        for (int r = 0; r < NUM_RAYS; r++) {
            /* 预测时间 Tpred = Σ(L/v) */
            float tPred = 0;
            float sumL2 = 0;
            for (uint8_t p = 0; p < rays[r].n; p++) {
                uint8_t idx = rays[r].pix[p];
                float   L   = rays[r].len[p];
                if (vmap[idx] > 0.001f) tPred += L / vmap[idx];
                sumL2 += L * L;
            }
            if (sumL2 < 0.001f) continue;

            float dT = rayT[r] - tPred;  /* 误差 */

            /* 更新每个像素的声慢度(1/v) */
            for (uint8_t p = 0; p < rays[r].n; p++) {
                uint8_t idx = rays[r].pix[p];
                float   L   = rays[r].len[p];
                float slow = 1.0f / vmap[idx];
                slow += ART_LAMBDA * dT * L / sumL2;
                if (slow > 0.001f) vmap[idx] = 1.0f / slow;
                if (vmap[idx] < ART_V_MIN) vmap[idx] = ART_V_MIN;
                if (vmap[idx] > ART_V_MAX) vmap[idx] = ART_V_MAX;
            }
        }
    }
}

/* ========= 异常分类 =========
 * 声速偏高 (> +1.5σ) = 空洞  (空气V>GrainV)
 * 声速偏低 (< -2.0σ) = 受潮
 * 轻度偏低           = 压实
 */
void Tomo_Classify(float* vmap, uint8_t* amap, uint8_t* mainA, float* conf) {
    /* 计算圆内(排除角落)均值和标准差 */
    float sum = 0, sum2 = 0;
    int   cnt = 0;
    for (int i = 0; i < GRID_PIX; i++) {
        float cx = (float)(i % GRID_N) * PIXEL_MM - 100.0f + PIXEL_MM*0.5f;
        float cy = (float)(i / GRID_N) * PIXEL_MM - 100.0f + PIXEL_MM*0.5f;
        if (cx*cx + cy*cy <= 90.0f*90.0f) {
            sum  += vmap[i];
            sum2 += vmap[i] * vmap[i];
            cnt++;
        }
    }
    float avg = (cnt>0) ? sum/cnt : V_GRAIN_MS;
    float std = (cnt>0) ? sqrtf(sum2/cnt - avg*avg) : 0.0f;
    if (std < 0.001f) std = 0.05f;  /* 避免除零 */

    int anomCnt[A_NUM] = {0};
    for (int i = 0; i < GRID_PIX; i++) {
        float cx = (float)(i % GRID_N) * PIXEL_MM - 100.0f + PIXEL_MM*0.5f;
        float cy = (float)(i / GRID_N) * PIXEL_MM - 100.0f + PIXEL_MM*0.5f;
        if (cx*cx + cy*cy > 90.0f*90.0f) {
            amap[i] = A_NORM; continue;
        }
        float d = vmap[i] - avg;
        if      (d >  1.5f*std) { amap[i] = A_VOID;  anomCnt[A_VOID]++; }
        else if (d < -2.0f*std) { amap[i] = A_WET;   anomCnt[A_WET]++; }
        else if (d < -1.0f*std) { amap[i] = A_CMP;   anomCnt[A_CMP]++; }
        else                     { amap[i] = A_NORM;  anomCnt[A_NORM]++; }
    }

    /* 找占比最高的异常类型 */
    int mxC = 0;
    *mainA = A_NORM;
    for (int i = 1; i < A_NUM; i++) {
        if (anomCnt[i] > mxC) { mxC = anomCnt[i]; *mainA = (uint8_t)i; }
    }

    /* 置信度 = 异常像素占圆内像素比 */
    *conf = (cnt>0) ? (float)mxC / (float)cnt : 0.0f;
    if (*conf > 1.0f) *conf = 1.0f;
}
