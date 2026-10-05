import { describe, expect, it } from 'vitest'
import { smokeIndex, smokeReading } from './smoke-index'

describe('site PA7 smoke reference', () => {
  it('matches reference points, interpolates and clamps without using AO', () => {
    for (const [mv, index] of [[0, 0], [330, 0], [350, 0], [600, 13], [800, 23], [850, 25], [900, 28], [1000, 33], [1175, 43], [1500, 60], [2000, 80], [2100, 84], [2500, 100], [3300, 100]]) expect(smokeIndex(mv)).toBe(index)
    let previous = 0
    for (let mv = 0; mv <= 3600; mv++) { const index = smokeIndex(mv)!; expect(index).toBeGreaterThanOrEqual(previous); expect(index).toBeLessThanOrEqual(100); previous = index }
    expect(smokeReading({ valid: true, raw: 1000, pa7_mv: 850, ao_mv: 1700, age_ms: 1999 }, true)).toBe(25)
  })
  it('keeps unavailable and stale data distinct from a zero index', () => {
    for (const mv of [NaN, Infinity, -1, 3601]) expect(smokeIndex(mv)).toBeNull()
    const mq2 = { valid: true, raw: 1000, pa7_mv: 850, ao_mv: 1700, age_ms: 0 }
    expect(smokeReading(mq2, false)).toBeNull()
    expect(smokeReading({ ...mq2, valid: false }, true)).toBeNull()
    expect(smokeReading({ ...mq2, pa7_mv: null }, true)).toBeNull()
    for (const age_ms of [null, -1, NaN, 2000, 5000]) expect(smokeReading({ ...mq2, age_ms }, true)).toBeNull()
  })
})
