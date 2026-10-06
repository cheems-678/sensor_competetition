import { describe, expect, it } from 'vitest'
import { initialSnapshot } from './types'
import { appendObservation, emptyHistory, HISTORY_LIMIT, HISTORY_WINDOW_MS, linePath, numericReading } from './environment-history'

function reading(id: number) {
  const snapshot = initialSnapshot()
  snapshot.connected = true; snapshot.port = 'COM32'
  snapshot.telemetry.sample_id = id; snapshot.telemetry.updated_at = '12:00:00'
  snapshot.telemetry.values = { master_temp: '0 ℃', slave_temp: '-2.5 °C', master_humidity: '50 %RH', slave_humidity: '--', master_pressure: '101325 Pa', slave_pressure: '100982 Pa' }
  return snapshot
}

describe('bounded environment observations', () => {
  it('parses units and valid zero while rejecting missing and malformed readings', () => {
    for (const value of ['--', '-- °C', '', 'NaN', 'Infinity Pa', '24x °C', '1 Pa extra']) expect(numericReading(value)).toBeNull()
    expect(numericReading('0 ℃')).toBe(0)
    expect(numericReading('-2.5 °C')).toBe(-2.5)
    expect(numericReading('50 %RH')).toBe(50)
    expect(numericReading('101325 Pa')).toBe(101325)
  })
  it('counts only distinct measurements, including identical readings in the same second', () => {
    const snapshot = reading(1)
    let history = appendObservation(emptyHistory(), snapshot, 1000)
    expect(history.points).toHaveLength(1)
    snapshot.revision++; snapshot.telemetry.mq2.age_ms = 500
    expect(appendObservation(history, snapshot, 1200)).toBe(history)
    snapshot.telemetry.sample_id++
    history = appendObservation(history, snapshot, 1500)
    expect(history.points).toHaveLength(2)
    expect(history.points[1].values.master_temp).toBe(0)
    expect(history.points[1].values.slave_humidity).toBeNull()
    snapshot.telemetry.sample_id++
    history = appendObservation(history, snapshot, 1400)
    expect(history.points[2].at).toBe(1501)
  })
  it('inserts a timeout gap once, retains disconnected history and starts a new connection cleanly', () => {
    const snapshot = reading(1)
    let history = appendObservation(emptyHistory(), snapshot, 1000)
    snapshot.telemetry.updated_at = null
    history = appendObservation(history, snapshot, 6000)
    expect(history.points).toHaveLength(2)
    expect(appendObservation(history, snapshot, 6500)).toBe(history)
    snapshot.telemetry.updated_at = '12:00:06'; snapshot.telemetry.sample_id++
    history = appendObservation(history, snapshot, 7000)
    expect(linePath(history.points, 'master_temp', at => at, value => value).match(/M/g)).toHaveLength(2)
    snapshot.connected = false
    history = appendObservation(history, snapshot, 8000)
    expect(history.points).toHaveLength(3)
    snapshot.connected = true; snapshot.telemetry.sample_id++
    history = appendObservation(history, snapshot, 9000)
    expect(history.points).toHaveLength(1)
    snapshot.port = 'COM6'; snapshot.telemetry.sample_id++
    expect(appendObservation(history, snapshot, 10000).points).toHaveLength(1)
  })
  it('bounds both the time window and observation count', () => {
    const snapshot = reading(1)
    let history = emptyHistory()
    for (let id = 1; id <= HISTORY_LIMIT + 5; id++) { snapshot.telemetry.sample_id = id; history = appendObservation(history, snapshot, id) }
    expect(history.points).toHaveLength(HISTORY_LIMIT)
    snapshot.telemetry.sample_id++
    history = appendObservation(history, snapshot, HISTORY_WINDOW_MS + HISTORY_LIMIT + 10)
    expect(history.points).toHaveLength(1)
  })
})
