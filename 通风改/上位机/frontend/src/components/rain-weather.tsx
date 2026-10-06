import { useEffect, useId, useRef, useState } from 'react'
import { CloudRain, Pause, Play } from 'lucide-react'
import { Button } from './button'
import { HelpDetails } from './help-details'
import type { Snapshot } from '../lib/types'
type RainReading = Snapshot['telemetry']['rain']
export type WeatherState = 'dry' | 'rain' | 'unknown'
export function weatherState(rain: RainReading | undefined, connected: boolean): WeatherState {
  if (!connected || rain?.source !== 'master') return 'unknown'
  return rain.state === 0 ? 'dry' : rain.state === 1 ? 'rain' : 'unknown'
}
export const weatherLabels: Record<WeatherState, string> = { dry: '无雨', rain: '有雨', unknown: '状态未知' }
function WeatherGraphic({ state, compact = false }: { state: WeatherState; compact?: boolean }) {
  const id = useId().replace(/:/g, '')
  return <svg className={`weather-graphic weather-${state}`} viewBox="0 0 400 280" role="img" aria-label={state === 'dry' ? '太阳：无雨' : state === 'rain' ? '云朵与雨滴：有雨' : '灰色云朵：状态未知'}>
    <defs>
      <radialGradient id={`${id}-sun`}><stop stopColor="#ffe7a1" /><stop offset="1" stopColor="#f9b541" /></radialGradient>
      <linearGradient id={`${id}-cloud`} x2="0" y2="1"><stop stopColor={state === 'unknown' ? '#43576b' : '#6594b4'} /><stop offset="1" stopColor={state === 'unknown' ? '#253849' : '#243e5d'} /></linearGradient>
    </defs>
    {!compact && <g className="weather-horizon" aria-hidden="true"><ellipse cx="200" cy="247" rx="155" ry="17" /><path d="M30 247H370M75 260H325" /></g>}
    {state === 'dry' ? <g>
      <circle className="sun-glow weather-motion" cx="200" cy="110" r="85" fill="#ffc84e" opacity=".07" />
      <g className="sun-rays weather-motion" stroke="#f9c957" strokeWidth="5" strokeLinecap="round">{Array.from({ length: 12 }, (_, i) => <path key={i} d="M200 40V52" transform={`rotate(${i * 30} 200 110)`} />)}</g>
      <circle cx="200" cy="110" r="42" fill={`url(#${id}-sun)`} stroke="#ffe6a0" strokeWidth="2" />
      <circle cx="188" cy="98" r="30" fill="none" stroke="#fff3cc" strokeOpacity=".3" />
    </g> : <g>
      <g className={state === 'rain' ? 'weather-cloud weather-motion' : 'weather-cloud'}>
        <path d="M117 151C93 151 91 113 116 106C117 76 153 60 177 80C205 41 267 59 270 98C312 95 328 151 287 151Z" fill={`url(#${id}-cloud)`} stroke={state === 'unknown' ? '#657f95' : '#87b9d7'} strokeWidth="2" />
        <path d="M127 108C131 89 152 84 169 93" fill="none" stroke="#c1deee" strokeOpacity=".3" strokeWidth="3" strokeLinecap="round" />
      </g>
      {state === 'rain' ? <g aria-hidden="true">
        {Array.from({ length: compact ? 8 : 16 }, (_, i) => <g key={i} className={compact ? 'rain-mark' : 'rain-drop weather-motion'} style={{ animationDelay: `${-i * .19}s`, animationDuration: `${1.35 + i % 5 * .18}s` }}>
          <path d={`M${compact ? 112 + i * 23 : 83 + i * 16} ${compact ? 177 : 153 + i % 3 * 10}l-7 17`} />
        </g>)}
        {!compact && Array.from({ length: 5 }, (_, i) => <ellipse className="rain-ripple weather-motion" key={i} cx={88 + i * 56} cy={242 + i % 2 * 6} rx="22" ry="5" style={{ animationDelay: `${-i * .45}s` }} />)}
      </g> : <text x="200" y="132" textAnchor="middle" fill="#acbfd0" fontSize="37" fontFamily="Segoe UI, sans-serif">?</text>}
    </g>}
  </svg>
}
export function RainIndicator({ state }: { state: WeatherState }) {
  return <div className={`weather-indicator weather-${state}`} role="status" aria-label="总览雨滴状态">
    <WeatherGraphic state={state} compact /><div><span>主机雨滴</span><strong>{weatherLabels[state]}</strong></div>
  </div>
}
export function RainMonitor({ state, updatedAt, active }: { state: WeatherState; updatedAt: string | null; active: boolean }) {
  const [paused, setPaused] = useState(() => window.matchMedia?.('(prefers-reduced-motion: reduce)').matches ?? false)
  const [documentVisible, setDocumentVisible] = useState(() => !document.hidden)
  const [inViewport, setInViewport] = useState(true)
  const sceneRef = useRef<HTMLDivElement>(null)
  useEffect(() => {
    const visibility = () => setDocumentVisible(!document.hidden)
    document.addEventListener('visibilitychange', visibility)
    const observer = typeof IntersectionObserver !== 'undefined' ? new IntersectionObserver(entries => setInViewport(entries[0]?.isIntersecting ?? true)) : null
    if (sceneRef.current) observer?.observe(sceneRef.current)
    return () => { document.removeEventListener('visibilitychange', visibility); observer?.disconnect() }
  }, [])
  const running = active && documentVisible && inViewport && !paused && state !== 'unknown'
  return <section className="panel rain-monitor" aria-labelledby="rain-monitor-title">
    <div className="rain-monitor-heading"><div className="heading-name"><CloudRain size={18} /><h2 id="rain-monitor-title">主机雨滴视窗</h2></div><Button variant="outline" size="small" onClick={() => setPaused(value => !value)}>{paused ? <Play size={14} /> : <Pause size={14} />}{paused ? '继续动画' : '暂停动画'}</Button></div>
    <div className={`rain-monitor-body weather-${state}`}>
      <div className="weather-scene" ref={sceneRef} data-running={running} aria-label="雨滴状态动画"><WeatherGraphic state={state} /></div>
      <div className="rain-details"><span className="page-eyebrow">RAIN / LIVE STATUS</span><div className="weather-reading" role="status" aria-label="雨滴监测状态">{weatherLabels[state]}</div>
        <dl><div><dt>传感器来源</dt><dd>主机 PA11 · 雨滴 DO</dd></div><div><dt>最近遥测更新</dt><dd>{updatedAt ?? '--'}</dd></div></dl>
      </div>
    </div>
    <HelpDetails><p>不测量雨量或降雨强度，不自动联动风机或窗户。</p></HelpDetails>
  </section>
}
