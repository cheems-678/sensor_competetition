import type { SensorReview } from '../lib/types'

const names = { rain: '雨滴检测', smoke: '烟雾相对指数', distance: '粮面测距' }
export function SensorChecks({ readings }: { readings: SensorReview['readings'] }) {
  return <div className="sensor-checks" aria-label="雨滴、烟雾和测距检测状态">{(['rain', 'smoke', 'distance'] as const).map(kind => {
    const sensor = readings[kind]
    return <div key={kind} className={`sensor-check ${sensor?.state ?? 'unavailable'}`}><strong>{names[kind]}</strong><span className="sensor-check-state" role="status" aria-label={`${names[kind]}预警状态`}>{sensor?.message ?? '数据不可用'}</span><p>{sensor?.current == null ? '--' : kind === 'rain' ? sensor.current === 1 ? '下雨 · 1' : '无雨 · 0' : `${sensor.current}${kind === 'distance' ? ' cm' : ' / 100'}`}</p></div>
  })}</div>
}
