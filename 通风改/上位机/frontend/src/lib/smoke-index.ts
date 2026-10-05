import type { Snapshot } from './types'

// Site observations supplied by the operator. PA7 millivolts, not AO or ppm.
export const smokeReferencePoints = [[350, 0], [850, 25], [1500, 60], [2500, 100]] as const

export function smokeIndex(pa7Mv: number): number | null {
  if (!Number.isFinite(pa7Mv) || pa7Mv < 0 || pa7Mv > 3600) return null
  if (pa7Mv <= 350) return 0
  for (let i = 1; i < smokeReferencePoints.length; i++) {
    const [highMv, highIndex] = smokeReferencePoints[i]
    const [lowMv, lowIndex] = smokeReferencePoints[i - 1]
    if (pa7Mv <= highMv) return Math.round(lowIndex + (pa7Mv - lowMv) * (highIndex - lowIndex) / (highMv - lowMv))
  }
  return 100
}

export function smokeReading(mq2: Snapshot['telemetry']['mq2'], connected: boolean): number | null {
  if (!connected || !mq2.valid || mq2.pa7_mv === null || mq2.age_ms === null || !Number.isFinite(mq2.age_ms) || mq2.age_ms < 0 || mq2.age_ms >= 2000) return null
  return smokeIndex(mq2.pa7_mv)
}

export function smokeLevel(index: number | null): string {
  if (index === null) return '数据不可用'
  if (index <= 5) return '接近基线'
  if (index < 25) return '轻微升高'
  if (index < 60) return '轻度烟雾参考'
  if (index < 80) return '较高烟雾参考'
  return '浓烟参考'
}
