// Visual simulation only. These values never enter telemetry or device commands.
export const temperatureStops = [
  { temperature: 16, color: [37, 99, 235], label: '蓝' },
  { temperature: 20, color: [34, 211, 238], label: '青' },
  { temperature: 24, color: [67, 221, 176], label: '绿' },
  { temperature: 28, color: [190, 213, 104], label: '黄绿' },
  { temperature: 32, color: [250, 204, 21], label: '黄' },
  { temperature: 36, color: [251, 146, 60], label: '橙' },
] as const

export function simulatedTemperature(height: number, seconds: number, angle = 0): number {
  const level = Math.max(0, Math.min(1, (height + 1.1) / 2.12))
  return 18 + level * 14 + .85 * Math.sin(seconds * .24 + level * 1.6)
    + .2 * Math.sin(angle * 2 + seconds * .17) * Math.sin(Math.PI * level)
}

export function temperatureColor(temperature: number): [number, number, number] {
  const bounded = Math.max(16, Math.min(36, temperature))
  const index = Math.min(temperatureStops.length - 2, Math.floor((bounded - 16) / 4))
  const a = temperatureStops[index], b = temperatureStops[index + 1]
  const weight = (bounded - a.temperature) / (b.temperature - a.temperature)
  return a.color.map((value, channel) => Math.round(value + (b.color[channel] - value) * weight)) as [number, number, number]
}

export const temperatureGradient = `linear-gradient(90deg, ${temperatureStops.map(stop => `rgb(${stop.color.join(',')}) ${(stop.temperature - 16) * 5}%`).join(', ')})`
