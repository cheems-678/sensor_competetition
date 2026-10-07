import { Ruler } from 'lucide-react'
import { useEffect, useId, useRef, useState } from 'react'
import type { SensorCheck, UltrasonicReading } from '../lib/types'
import { HelpDetails } from './help-details'
import { useVisualMotion } from '../lib/use-visual-motion'

export function UltrasonicMonitor({ reading, connected, active = true, nearDistanceMm = 100, warning }: { reading: UltrasonicReading; connected: boolean; active?: boolean; nearDistanceMm?: number; warning?: SensorCheck }) {
  const valid = connected && reading.valid && reading.age_ms !== null && reading.age_ms < 2000 && reading.distance_mm !== null && reading.distance_mm >= 50 && reading.distance_mm <= 500
  const near = valid && (warning ? warning.state === 'abnormal' : reading.distance_mm! < nearDistanceMm)
  const motion = useVisualMotion(active)
  const target = useRef(250)
  const shown = useRef(250)
  const accepted = useRef<number | null>(null)
  const [surface, setSurface] = useState(250)
  useEffect(() => {
    if (!valid) { accepted.current = null; return }
    if (accepted.current === null || Math.abs(reading.distance_mm! - accepted.current) >= 2) {
      accepted.current = reading.distance_mm
      target.current = 150 + (reading.distance_mm! - 50) / 450 * 144
    }
    if (motion.reduced) { shown.current = target.current; setSurface(target.current); return }
    if (!motion.running) return
    let frame = 0, last = performance.now()
    const animate = (now: number) => {
      const delta = Math.min(64, Math.max(0, now - last)); last = now
      shown.current += (target.current - shown.current) * (1 - Math.exp(-delta / 280))
      if (Math.abs(target.current - shown.current) < .15) shown.current = target.current
      setSurface(shown.current)
      if (shown.current !== target.current) frame = requestAnimationFrame(animate)
    }
    frame = requestAnimationFrame(animate)
    return () => cancelAnimationFrame(frame)
  }, [valid, reading.distance_mm, motion.running, motion.reduced])
  const distance = (value: number | null) => valid && value !== null ? (value / 10).toFixed(1) : '--'
  const id = useId().replace(/:/g, '')
  return <section ref={motion.ref} className="panel sensor-panel ultrasonic-panel" aria-labelledby="ultrasonic-title">
    <div className="panel-heading"><div className="heading-name"><Ruler size={18} /><h2 id="ultrasonic-title">仓顶到粮面距离</h2></div><span className="device-label">HC-SR04</span></div>
    <div className={`ultrasonic-scene ${valid ? 'measurement-valid' : 'measurement-unavailable'} ${near ? 'distance-near' : ''}`}>
      <svg className="ultrasonic-granary" viewBox="0 0 600 380" role="img" aria-label="粮仓剖面：仓顶传感器到水平粮面的竖直虚线表示距离">
        <defs>
          <linearGradient id={`${id}-wall`} x2="1" y2="0"><stop stopColor="#142c41" /><stop offset=".5" stopColor="#102235" /><stop offset="1" stopColor="#1b344b" /></linearGradient>
          <linearGradient id={`${id}-grain`} x2="0" y2="1"><stop stopColor="#b39759" stopOpacity=".6" /><stop offset="1" stopColor="#514635" stopOpacity=".8" /></linearGradient>
          <pattern id={`${id}-kernels`} width="24" height="20" patternUnits="userSpaceOnUse"><ellipse cx="5" cy="6" rx="3" ry="1.4" fill="#dbc18a" opacity=".45" transform="rotate(-25 5 6)" /><ellipse cx="17" cy="15" rx="3" ry="1.4" fill="#b39b6e" opacity=".5" transform="rotate(30 17 15)" /></pattern>
          <marker id={`${id}-arrow`} markerWidth="8" markerHeight="8" refX="4" refY="4" orient="auto-start-reverse"><path d="M8 4L0 0L0 8Z" fill="currentColor" /></marker>
        </defs>
        <ellipse cx="210" cy="349" rx="153" ry="12" fill="#050d18" opacity=".5" />
        <path d="M90 111L210 42L330 111V329Q210 354 90 329Z" fill={`url(#${id}-wall)`} stroke="#7696b3" strokeWidth="2" />
        <path d="M90 111L210 42L330 111M90 111H330" fill="none" stroke="#8cb0cd" strokeWidth="2" />
        <g className="grain-surface" opacity={valid ? 1 : .25} data-surface-y={surface.toFixed(2)}>
          <path d={`M107 ${surface}H313V316Q210 336 107 316Z`} fill={`url(#${id}-grain)`} />
          <path d={`M107 ${surface}H313V316Q210 336 107 316Z`} fill={`url(#${id}-kernels)`} />
          <path d={`M107 ${surface}H313`} stroke="#ddc48b" strokeWidth="3" />
        </g>
        <path d="M101 130V232M319 130V232" stroke="#52728c" opacity=".45" />
        <g className="ultrasonic-sensor">
          <path d="M210 82V100" stroke="#90afc9" strokeWidth="2" />
          <rect x="188" y="99" width="44" height="20" rx="4" fill="#203e51" stroke="#75d8e3" strokeWidth="1.5" />
          <circle cx="199" cy="109" r="5" fill="#091d2b" stroke="#96dae5" /><circle cx="221" cy="109" r="5" fill="#091d2b" stroke="#96dae5" />
        </g>
        <g className="ultrasonic-dimension">
          <path d={`M195 124H225M195 ${surface - 4}H225`} fill="none" stroke="currentColor" strokeWidth="2" />
          <line x1="210" y1="132" x2="210" y2={surface - 12} stroke="currentColor" strokeWidth="2.5" strokeDasharray="6 7" markerStart={`url(#${id}-arrow)`} markerEnd={`url(#${id}-arrow)`} />
          <path d={`M225 ${(124 + surface) / 2}H351`} fill="none" stroke="currentColor" strokeWidth="1.2" opacity=".65" />
          <circle cx="210" cy={(124 + surface) / 2} r="3" fill="currentColor" />
        </g>
        <text x="210" y="25" textAnchor="middle" className="ultrasonic-diagram-label">仓顶传感器</text>
        <text x="210" y={Math.min(312, surface + 34)} textAnchor="middle" className="ultrasonic-grain-label">{valid ? '粮面' : '粮面未知'}</text>
      </svg>
      <div className="ultrasonic-annotation">
        <span className="ultrasonic-annotation-label">到粮面表面的距离</span>
        <div className="ultrasonic-distance" aria-label="仓顶到粮面距离"><strong>{distance(reading.distance_mm)}</strong><span>cm</span></div>
      </div>
      <span className="ultrasonic-diagram-caption">5–50 cm 相对粮面示意</span>
    </div>
    <div className="metrics-grid ultrasonic-metrics">
      <div className="metric"><span className="metric-label">原始距离</span><div className="metric-value"><span>{distance(reading.raw_mm)}</span><span className="metric-unit">cm</span></div></div>
      <div className="metric"><span className="metric-label">回波脉宽</span><div className="metric-value"><span>{valid ? reading.pulse_us : '--'}</span><span className="metric-unit">μs</span></div></div>
      <div className="metric"><span className="metric-label">已知采样年龄</span><div className="metric-value"><span>{valid ? reading.age_ms : '--'}</span><span className="metric-unit">ms</span></div></div>
    </div>
    <div className="link-row"><span>测距状态</span><span className={`link-state ${near ? 'warning-error' : valid ? 'online' : ''}`} role="status" aria-label="测距状态">{near ? warning?.message ?? `粮面距离过近（<${nearDistanceMm / 10} cm）` : valid ? '有效测量' : '数据不可用'}</span></div>
    <HelpDetails id="ultrasonic-help"><p>距离越小，示意粮面越高；按5–50 cm相对范围绘制，未标定实际仓高，不表示粮高或储量。主读数采用最近5次有效样本中位数；动画另外使用2 mm死区和缓动，不改变数值或预警判定。有效下限5 cm；小于10 cm立即预警，≥11 cm连续有效3秒恢复。</p><p>约每秒随遥测更新；故障、失联或已知采样年龄达到2秒时清空并标记粮面未知。已知年龄不包含无法准确计量的无线在途延迟。</p></HelpDetails>
  </section>
}
