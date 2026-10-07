import { projectGranaryPoint, type GranaryView } from './granary-model'

export const acousticChannels = ['PA0', 'PA4', 'PA6', 'PB0', 'PA5'] as const
export const acousticPositions = acousticChannels.map((pin, index) => ({
  channel: index + 1, pin, angle: index * Math.PI * 2 / 5,
  point: [Math.cos(index * Math.PI * 2 / 5) * .86, .45, Math.sin(index * Math.PI * 2 / 5) * .86] as const,
}))
export type AcousticState = 'normal' | 'abnormal' | 'unavailable' | 'unset'
export interface AcousticReading { value: number | null; threshold: number | null; state: AcousticState; label: string }

export function acousticReading(raw: string | undefined, threshold: number | null, connected: boolean): AcousticReading {
  const text = raw?.trim() ?? ''
  const number = /^\d+$/.test(text) ? Number(text) : NaN
  const value = connected && Number.isInteger(number) && number >= 0 && number <= 4095 ? number : null
  const configured = threshold !== null && Number.isInteger(threshold) && threshold >= 0 && threshold <= 4095
  if (value === null) return { value, threshold, state: 'unavailable', label: '数据不可用' }
  if (!configured) return { value, threshold: null, state: 'unset', label: '待设置阈值' }
  return value > threshold! ? { value, threshold, state: 'abnormal', label: '幅度超限' }
    : { value, threshold, state: 'normal', label: '未超阈值' }
}

export function parseAcousticThresholds(drafts: string[]): { values: (number | null)[]; error: string } {
  if (drafts.length !== 5) return { values: [], error: '需要五路阈值' }
  const values: (number | null)[] = []
  for (let index = 0; index < drafts.length; index++) {
    const text = drafts[index].trim()
    if (!text) { values.push(null); continue }
    const value = Number(text)
    if (!/^\d+$/.test(text) || !Number.isInteger(value) || value > 4095) {
      return { values: [], error: `声音 ${index + 1} 阈值须为 0～4095 的整数，或留空` }
    }
    values.push(value)
  }
  return { values, error: '' }
}

export function projectAcousticSensors(width: number, height: number, view: GranaryView) {
  return acousticPositions.map(sensor => ({ ...sensor, screen: projectGranaryPoint(sensor.point, width, height, view) }))
}
