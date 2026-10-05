import { describe, expect, it } from 'vitest'
import { simulatedTemperature, temperatureColor, temperatureStops } from './simulated-temperature'

describe('visual temperature simulation', () => {
  it('keeps the lower region cool and the upper region warm throughout the animation', () => {
    for (const seconds of [0, 5, 17, 40, 120, 1800, 1000000]) {
      let previous = -Infinity
      for (let level = 0; level <= 20; level++) {
        const value = simulatedTemperature(-1.1 + level / 20 * 2.12, seconds, 2.4)
        expect(value).toBeGreaterThanOrEqual(16)
        expect(value).toBeLessThanOrEqual(36)
        expect(value).toBeGreaterThan(previous)
        previous = value
      }
      expect(simulatedTemperature(-1.1, seconds)).toBeLessThan(20)
      expect(simulatedTemperature(1.02, seconds)).toBeGreaterThan(30)
    }
    expect(simulatedTemperature(-1.1, 5)).not.toBe(simulatedTemperature(-1.1, 0))
    expect(Math.abs(simulatedTemperature(1.02, 5.25) - simulatedTemperature(1.02, 5))).toBeLessThan(.06)
  })

  it('uses the stated legend colors exactly and clamps values outside the spectrum', () => {
    for (const stop of temperatureStops) expect(temperatureColor(stop.temperature)).toEqual(stop.color)
    expect(temperatureColor(-40)).toEqual(temperatureStops[0].color)
    expect(temperatureColor(80)).toEqual(temperatureStops.at(-1)?.color)
    expect(temperatureColor(18)).toEqual([36, 155, 237])
  })
})
