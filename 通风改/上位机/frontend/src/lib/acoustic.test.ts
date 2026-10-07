import { describe, expect, it } from 'vitest'
import { acousticChannels, acousticPositions, acousticReading, parseAcousticThresholds, projectAcousticSensors } from './acoustic'
import { adjustGranaryView, defaultGranaryView, projectGranaryPoint } from './granary-model'

describe('local sound amplitude checks', () => {
  it('treats equality/zero/bounds as valid, no historical maximum or hysteresis', () => {
    expect(acousticReading('0', 0, true).state).toBe('normal')
    expect(acousticReading('100', 100, true).state).toBe('normal')
    expect(acousticReading('101', 100, true).state).toBe('abnormal')
    expect(acousticReading('99', 100, true).state).toBe('normal')
    expect(acousticReading('4095', 4095, true).state).toBe('normal')
    expect(acousticReading('4095', 0, true).state).toBe('abnormal')
  })
  it('never turns missing/invalid/disconnected data into normal or zero', () => {
    for (const raw of [undefined, '', ' ', '--', 'NaN', 'Infinity', '-1', '4096', '1.2', '1e3']) {
      expect(acousticReading(raw, 0, true)).toMatchObject({ value: null, state: 'unavailable' })
    }
    expect(acousticReading('1000', 0, false).state).toBe('unavailable')
    expect(acousticReading('0', null, true)).toMatchObject({ value: 0, state: 'unset' })
    expect(acousticReading('--', null, true).state).toBe('unavailable')
    expect(acousticReading('1', -1, true).state).toBe('unset')
  })
  it('parses five drafts atomically, blank disables and both limits accepted', () => {
    expect(parseAcousticThresholds(['0', '4095', '', ' 123 ', '0007'])).toEqual({ values: [0, 4095, null, 123, 7], error: '' })
    for (const draft of ['-1', '4096', '0.5', 'abc', '1e2', '+1', 'Infinity']) {
      const result = parseAcousticThresholds(['0', '12', draft, '', '4095'])
      expect(result.values).toEqual([])
      expect(result.error).toMatch(/声音 3/)
    }
    expect(parseAcousticThresholds([]).error).toBeTruthy()
  })
})

describe('wall sensor geometry shares the overview camera', () => {
  it('keeps pins in order with five unique equal-height inner-wall points 72 degrees apart', () => {
    expect(acousticChannels).toEqual(['PA0', 'PA4', 'PA6', 'PB0', 'PA5'])
    expect(acousticPositions.map(point => point.channel)).toEqual([1, 2, 3, 4, 5])
    for (const [index, sensor] of acousticPositions.entries()) {
      expect(sensor.angle).toBeCloseTo(index * 2 * Math.PI / 5)
      expect(sensor.point[1]).toBe(.45)
      expect(Math.hypot(sensor.point[0], sensor.point[2])).toBeCloseTo(.86)
    }
    expect(new Set(acousticPositions.map(sensor => sensor.point.join(','))).size).toBe(5)
  })
  it('rotates/resizes/zooms every marker without changing its channel and never culls rear points', () => {
    const initial = projectAcousticSensors(800, 400, defaultGranaryView())
    for (let i = 0; i < 20; i++) {
      const view = adjustGranaryView(defaultGranaryView(), i * .4, .1, 1.2)
      const points = projectAcousticSensors(800, 400, view)
      expect(points).toHaveLength(5)
      for (const point of points) {
        expect(point.screen).toEqual(projectGranaryPoint(point.point, 800, 400, view))
        expect(point.screen.every(Number.isFinite)).toBe(true)
        expect(point.pin).toBe(acousticChannels[point.channel - 1])
      }
    }
    expect(projectAcousticSensors(800, 400, adjustGranaryView(defaultGranaryView(), .4))).not.toEqual(initial)
    expect(projectAcousticSensors(400, 320, defaultGranaryView()).map(p => p.screen)).not.toEqual(initial.map(p => p.screen))
  })
})
