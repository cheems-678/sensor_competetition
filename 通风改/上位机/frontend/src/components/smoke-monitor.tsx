import { useEffect, useId, useRef, useState, type CSSProperties } from 'react'
import { Activity, Pause, Play } from 'lucide-react'
import { Button } from './button'
import { HelpDetails } from './help-details'
import { smokeLevel, smokeReading, smokeReferencePoints } from '../lib/smoke-index'
import type { SensorCheck, Snapshot } from '../lib/types'
export function SmokeMonitor({ mq2, connected, active, warning }: { mq2: Snapshot['telemetry']['mq2']; connected: boolean; active: boolean; warning?: SensorCheck }) {
  const index = smokeReading(mq2, connected)
  const abnormal = index !== null && (warning ? warning.state === 'abnormal' : index > 10)
  const [paused, setPaused] = useState(() => window.matchMedia?.('(prefers-reduced-motion: reduce)').matches ?? false)
  const [visible, setVisible] = useState(() => !document.hidden)
  const [inViewport, setInViewport] = useState(true)
  const sceneRef = useRef<HTMLDivElement>(null)
  const id = useId().replace(/:/g, '')
  useEffect(() => {
    const visibility = () => setVisible(!document.hidden)
    document.addEventListener('visibilitychange', visibility)
    const observer = typeof IntersectionObserver !== 'undefined' ? new IntersectionObserver(entries => setInViewport(entries[0]?.isIntersecting ?? true)) : null
    if (sceneRef.current) observer?.observe(sceneRef.current)
    return () => { document.removeEventListener('visibilitychange', visibility); observer?.disconnect() }
  }, [])
  const running = active && visible && inViewport && !paused && index !== null && index > 0
  const density = index === null || index === 0 ? 0 : 3 + Math.ceil(index / 5)
  const color = index === null ? '#7d93aa' : index < 25 ? '#8bcbdc' : index < 60 ? '#dbc091' : index < 80 ? '#efad65' : '#ef8068'
  const sceneStyle = { '--smoke-color': color, '--smoke-opacity': .12 + (index ?? 0) * .005, '--smoke-duration': `${8 - (index ?? 0) * .035}s` } as CSSProperties
  return <section className={`panel smoke-monitor ${abnormal ? 'smoke-abnormal' : ''}`} style={sceneStyle} aria-labelledby="smoke-monitor-title">
    <div className="rain-monitor-heading"><div className="heading-name"><Activity size={18} /><h2 id="smoke-monitor-title">烟雾监测</h2></div><Button variant="outline" size="small" onClick={() => setPaused(value => !value)}>{paused ? <Play size={14} /> : <Pause size={14} />}{paused ? '继续动画' : '暂停动画'}</Button></div>
    <div className="smoke-monitor-body">
      <div ref={sceneRef} className="smoke-scene" data-running={running} data-density={density} aria-label="烟雾强弱动画">
        <svg viewBox="0 0 480 320" className="smoke-graphic" role="img" aria-label={index === null ? '烟雾数据未知' : `相对烟雾指数 ${index} 的视觉示意`}>
          <defs><radialGradient id={`${id}-puff`}><stop stopColor={color} stopOpacity=".8" /><stop offset=".6" stopColor={color} stopOpacity=".35" /><stop offset="1" stopColor={color} stopOpacity="0" /></radialGradient></defs>
          <g className="smoke-room" fill="none" stroke="#40677f"><path d="M65 262V96L240 42L415 96V262ZM65 96L240 152L415 96M240 152V285M65 262L240 285L415 262" /><path d="M92 266V112M388 266V112M114 126L240 166L366 126" strokeOpacity=".25" /></g>
          <ellipse cx="240" cy="262" rx="142" ry="20" fill={color} opacity={index === null ? .04 : .04 + index * .001} />
          {Array.from({ length: density }, (_, i) => <g className="smoke-puff" key={i} style={{ animationDelay: `${-i * .43}s` }}><ellipse cx={125 + (i * 47) % 230} cy={225 - (i * 29) % 145} rx={32 + (index ?? 0) * .22 + i % 3 * 5} ry={26 + (index ?? 0) * .18} fill={`url(#${id}-puff)`} /></g>)}
          {index === null && <text x="240" y="182" textAnchor="middle" fill="#8196ad" fontSize="42">?</text>}
          {index === 0 && <g fill="#7fc5d5" opacity=".55"><path d="M171 191h35m-8-5 8 5-8 5M258 170h40m-8-5 8 5-8 5" fill="none" stroke="currentColor" strokeWidth="2" /></g>}
          <text x="240" y="307" textAnchor="middle" fill="#6289a3" fontSize="11" letterSpacing="3">MQ-2 / PA7 · RELATIVE SMOKE</text>
        </svg>
      </div>
      <div className="smoke-details">
        <div className="smoke-reading-label"><span>相对指数</span><span className="device-label">未标定</span></div>
        <div className="smoke-number" aria-label="相对烟雾指数"><strong>{index ?? '--'}</strong><span>/ 100</span></div>
        <div className="smoke-level" role="status" aria-label="烟雾强弱参考">{smokeLevel(index)}</div>
        <div className="smoke-scale" role={index === null ? 'img' : 'meter'} aria-label={index === null ? '烟雾指数不可用' : '烟雾指数刻度'} aria-valuemin={index === null ? undefined : 0} aria-valuemax={index === null ? undefined : 100} aria-valuenow={index ?? undefined} aria-valuetext={index === null ? undefined : String(index)}><span style={{ width: `${index ?? 0}%` }} /></div>
        <div className="smoke-scale-labels"><span>正常参考</span><span>烟雾增强</span><span>浓烟参考</span></div>
      </div>
    </div>
    <div className="link-row"><span>烟雾预警状态</span><span role="status" aria-label="烟雾预警状态" className={abnormal ? 'warning-error' : index === null ? '' : 'online'}>{index === null ? '数据不可用' : abnormal ? '相对指数超限（>10）' : '正常检测'}</span></div>
    <HelpDetails>
        <p >按你的现场电压观察建立经验参考，浓度未标定。指数不是浓度百分比或 ppm。</p>
        <div className="smoke-reference">{smokeReferencePoints.map(([mv, value]) => <div key={mv}><span>{(mv / 1000).toFixed(2)} V</span><strong>{value}</strong></div>)}</div>
        <p >PA7 输入电压 · 分段插值 · 基线固定 0.35 V<br />仅显示相对强弱，不自动触发风机或窗户。</p>
        <p>电压按从机配置的分压比例还原；采样有效不代表模块接线正常或预热完成。</p>
    </HelpDetails>
  </section>
}
