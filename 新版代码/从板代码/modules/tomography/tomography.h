/**
 * ART代数重建层析算法 - SPL版本
 *
 * 原理:
 *   8个换能器等分圆形(内径200mm)
 *   两两组合形成28条射线路径
 *   测量每条射线超声波穿越时间 T_i
 *   ART反解 16x16=256 像素的声速分布 v_j
 *
 * 公式: T_i = Σ (L_ij / v_j)   (L: 射线i在像素j中的路径长度)
 *
 * 更新: 1/v_j += λ * (T_meas - T_pred) * L_ij / Σ(L_ij^2)
 */

#ifndef __TOMOGRAPHY_H
#define __TOMOGRAPHY_H

#include "../../config.h"

/* 射线路径: 一条射线经过哪些像素, 各占多少mm */
typedef struct {
    uint8_t  pix[64];   /* 像素索引 */
    float    len[64];   /* 在该像素中的长度 mm */
    uint8_t  n;         /* 经过的像素数 */
    uint8_t  tx;        /* 发射端 */
    uint8_t  rx;        /* 接收端 */
} RayPath;

/* 全部API返回0=成功 */
void  Tomo_Init(void);                                /* 初始化: 计算所有射线 */
void  Tomo_Reconstruct(float* rayTimes_ms, float* vmap);  /* ART重建 */
void  Tomo_Classify(float* vmap, uint8_t* amap,         /* 异常分类 */
                    uint8_t* mainAnom, float* conf);
RayPath* Tomo_GetRays(void);                         /* 调试用 */

#endif /* __TOMOGRAPHY_H */
