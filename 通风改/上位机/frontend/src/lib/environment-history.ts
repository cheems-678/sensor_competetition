import { useEffect, useState } from 'react'
import type { Field, Snapshot } from './types'

export const HISTORY_WINDOW_MS = 10 * 60 * 1000
export const HISTORY_LIMIT = 1200
export interface EnvironmentPoint { at: number; values: Record<Field, number | null> }
export interface EnvironmentHistory {
  points: EnvironmentPoint[]; connected: boolean; port: string; sampleId: number; gap: boolean
}
export const emptyHistory = (): EnvironmentHistory => ({ points: [], connected: false, port: '', sampleId: 0, gap: false })
export const environmentFields: Field[] = ['master_temp', 'slave_temp', 'master_humidity', 'slave_humidity', 'master_pressure', 'slave_pressure']

export function numericReading(value: string): number | null {
  const match = value.match(/^\s*(-?\d+(?:\.\d+)?)\s*(?:℃|°C|%RH|%|Pa)?\s*$/u)
  const result = match ? Number(match[1]) : NaN
  return Number.isFinite(result) ? result : null
}

export function appendObservation(history: EnvironmentHistory, snapshot: Snapshot, now: number): EnvironmentHistory {
  if (!snapshot.connected) return history.connected ? { ...history, connected: false } : history
  const restarted = !history.connected || history.port !== snapshot.port || snapshot.telemetry.sample_id < history.sampleId
  const current = restarted ? { ...emptyHistory(), connected: true, port: snapshot.port } : history
  const { sample_id: sampleId, updated_at: updatedAt } = snapshot.telemetry
  if (!updatedAt) {
    if (!current.points.length || current.gap) return current
    const values = Object.fromEntries(environmentFields.map(field => [field, null])) as EnvironmentPoint['values']
    const at = Math.max(now, current.points.at(-1)!.at + 1)
    return { ...current, gap: true, points: [...current.points, { at, values }].filter(point => point.at >= at - HISTORY_WINDOW_MS).slice(-HISTORY_LIMIT) }
  }
  if (!Number.isSafeInteger(sampleId) || sampleId <= current.sampleId) return current
  const values = Object.fromEntries(environmentFields.map(field => [field, numericReading(snapshot.telemetry.values[field])])) as EnvironmentPoint['values']
  // Preserve increasing receipt times even if the system wall clock moves back.
  const at = Math.max(now, (current.points.at(-1)?.at ?? now - 1) + 1)
  const points = [...current.points, { at, values }].filter(point => point.at >= at - HISTORY_WINDOW_MS).slice(-HISTORY_LIMIT)
  return { ...current, sampleId, gap: false, points }
}

export function useEnvironmentHistory(snapshot: Snapshot) {
  const [history, setHistory] = useState(emptyHistory)
  useEffect(() => { setHistory(current => appendObservation(current, snapshot, Date.now())) }, [snapshot])
  return history.points
}

export function linePath(points: EnvironmentPoint[], field: Field, x: (at: number) => number, y: (value: number) => number): string {
  let continuous = false
  return points.map(point => {
    const value = point.values[field]
    if (value === null) { continuous = false; return '' }
    const command = continuous ? 'L' : 'M'
    continuous = true
    return `${command}${x(point.at).toFixed(2)},${y(value).toFixed(2)}`
  }).filter(Boolean).join(' ')
}
