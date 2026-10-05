import { useEffect, useId, useRef, useState, type CSSProperties } from 'react'
import { Activity, Pause, Play } from 'lucide-react'
import { Button } from './button'
import { smokeLevel, smokeReading, smokeReferencePoints } from '../lib/smoke-index'
import type { Snapshot } from '../lib/types'

export function SmokeMonitor({ mq2, connected, active }: { mq2: Snapshot['telemetry']['mq2']; connected: boolean; active: boolean }) {
  const index = smokeReading(mq2, connected)
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
  return <section className="panel smoke-monitor" style={sceneStyle} aria-labelledby="smoke-monitor-title">
    <div className="rain-monitor-heading"><div className="heading-name"><Activity size={18} /><h2 id="smoke-monitor-title">烟雾浓度视窗</h2></div><Button variant="outline" size="small" onClick={() => setPaused(value => !value)}>{paused ? <Play size={14} /> : <Pause size={14} />}{paused ? '继续动画' : '暂停动画'}</Button></div>
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
        <span className="scene-caption">{index === null ? '等待有效烟雾数据' : paused ? '动画已暂停 · 指数仍实时更新' : '烟团随相对指数变化 · 视觉示意'}</span>
      </div>
      <div className="smoke-details">
        <span className="page-eyebrow">SMOKE / RELATIVE INDEX</span>
        <div className="smoke-number" aria-label="相对烟雾指数"><strong>{index ?? '--'}</strong><span>/ 100</span></div>
        <div className="smoke-level" role="status" aria-label="烟雾强弱参考">{smokeLevel(index)}</div>
        <div className="smoke-scale" role={index === null ? 'img' : 'meter'} aria-label={index === null ? '烟雾指数不可用' : '烟雾指数刻度'} aria-valuemin={index === null ? undefined : 0} aria-valuemax={index === null ? undefined : 100} aria-valuenow={index ?? undefined} aria-valuetext={index === null ? undefined : String(index)}><span style={{ width: `${index ?? 0}%` }} /></div>
        <div className="smoke-scale-labels"><span>正常参考</span><span>烟雾增强</span><span>浓烟参考</span></div>
        <p className="smoke-note">按你的现场电压观察建立经验参考，浓度未标定。指数不是浓度百分比或 ppm。</p>
        <div className="smoke-reference">{smokeReferencePoints.map(([mv, value]) => <div key={mv}><span>{(mv / 1000).toFixed(2)} V</span><strong>{value}</strong></div>)}</div>
        <p className="smoke-note">PA7 输入电压 · 分段插值 · 基线固定 0.35 V<br />仅显示相对强弱，不自动触发风机或窗户。</p>
      </div>
    </div>
  </section>
}
