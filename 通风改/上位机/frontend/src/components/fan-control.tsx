import { useEffect, useRef, useState } from 'react'
import { ArrowUpRight, Fan } from 'lucide-react'
import type { FanState } from '../lib/types'
import { Button } from './button'
import { decimalDraft } from '../lib/duty'
import { useVisualMotion } from '../lib/use-visual-motion'

const adjustmentKeys = new Set(['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home', 'End', 'PageUp', 'PageDown'])

export function FanControl({ fan, enabled, active = true, submit }: { fan: FanState; enabled: boolean; active?: boolean; submit: (channel: number, duty: number | string) => void }) {
  const [draft, setDraft] = useState(String(fan.duty))
  const draftRef = useRef(draft)
  const pointerActive = useRef(false)
  const dirty = useRef(false)
  const keys = useRef(new Set<string>())
  const motion = useVisualMotion(active)
  const confirmed = fan.confirmed_duty ?? (fan.status.startsWith('已确认 ') ? fan.duty : null)
  const speed = confirmed == null || confirmed === 0 ? 'off' : confirmed >= 100 ? 'high' : confirmed >= 90 ? 'medium' : 'low'
  const updateDraft = (value: string) => { dirty.current = true; draftRef.current = value; setDraft(value) }
  useEffect(() => {
    if (!dirty.current && !pointerActive.current && !keys.current.size) { draftRef.current = String(fan.duty); setDraft(String(fan.duty)) }
  }, [fan.duty])
  useEffect(() => {
    if (!enabled || !active) { pointerActive.current = false; keys.current.clear() }
  }, [enabled, active])
  const number = decimalDraft(draft)
  const sliderValue = Number.isFinite(number) ? Math.max(0, Math.min(100, number)) : 0
  const currentLevel = sliderValueLevel(sliderValue)
  const commit = () => {
    if (!enabled || !active) return
    const raw = draftRef.current
    const numeric = decimalDraft(raw)
    if (Number.isFinite(numeric) && numeric >= 0 && numeric <= 100) {
      const normalized = Math.floor(numeric + 0.5)
      updateDraft(String(normalized))
      dirty.current = false
      submit(fan.channel, normalized)
    } else submit(fan.channel, raw)
  }
  const shortcut = (duty: number) => {
    if (!enabled || !active) return
    keys.current.clear(); pointerActive.current = false
    updateDraft(String(duty)); dirty.current = false; submit(fan.channel, duty)
  }
  return <section ref={motion.ref} className={`fan-control fan-card speed-${speed} ${motion.running && speed !== 'off' ? 'fan-running' : ''}`} aria-labelledby={`fan-title-${fan.channel}`}>
    <div className="fan-header">
      <div className="control-name"><Fan size={17} strokeWidth={1.7} /><h3 id={`fan-title-${fan.channel}`}>风机 {fan.channel}</h3><span className="pin">{fan.pin}</span></div>
      <span className="micro-label">PWM 占空比</span>
    </div>
    <div className="fan-visual" aria-label={confirmed == null ? 'PWM状态未知' : `已确认PWM ${confirmed}%，运行示意`}>
      <div className="fan-ring"><Fan className="fan-rotor" size={86} strokeWidth={1.2} /><span className="fan-hub" /></div>
      <div className="fan-air" aria-hidden="true">{[0, 1, 2, 3, 4].map(index => <i key={index} style={{ animationDelay: `${-index * .22}s`, top: `${index * 17 + 10}%` }} />)}</div>
    </div>
    <div className="fan-level-status">{confirmed == null ? '状态未知' : `${sliderValueLevel(confirmed)} · 已确认 ${confirmed}%`}<span>PWM 运行示意</span></div>
    <div className="fan-presets" role="group" aria-label={`风机 ${fan.channel} 快捷档位`}>{[{ label: '高', duty: 100 }, { label: '中', duty: 90 }, { label: '低', duty: 80 }, { label: '关闭', duty: 0 }].map(item => <button key={item.label} type="button" disabled={!enabled} aria-pressed={currentLevel === item.label} onClick={() => shortcut(item.duty)}><strong>{item.label}</strong><small>{item.duty}%</small></button>)}</div>
    <div className="fan-input-row">
      <input
        type="range" min="0" max="100" step="1" value={sliderValue}
        aria-label={`风机 ${fan.channel} 占空比滑块`}
        style={{ '--range-fill': `${sliderValue}%` } as React.CSSProperties}
        onChange={event => updateDraft(event.target.value)}
        onPointerDown={event => {
          if (event.button !== 0) return
          pointerActive.current = true
          event.currentTarget.setPointerCapture?.(event.pointerId)
        }}
        onPointerUp={() => {
          if (!pointerActive.current) return
          pointerActive.current = false
          commit()
        }}
        onPointerCancel={() => { pointerActive.current = false }}
        onLostPointerCapture={() => { pointerActive.current = false }}
        onKeyDown={event => { if (adjustmentKeys.has(event.key)) keys.current.add(event.key) }}
        onKeyUp={event => { if (adjustmentKeys.has(event.key) && keys.current.delete(event.key)) commit() }}
        onBlur={() => { keys.current.clear(); pointerActive.current = false }}
      />
      <div className="duty-input"><input aria-label={`风机 ${fan.channel} 占空比`} inputMode="decimal" value={draft} onChange={event => updateDraft(event.target.value)} /><span>%</span></div>
      <Button size="small" variant="outline" disabled={!enabled} onClick={commit} aria-label={`发送风机 ${fan.channel} 占空比`}>发送<ArrowUpRight size={14} /></Button>
    </div>
    <p className="fan-status" role="status"><span className="status-dot muted" />{fan.status}</p>
  </section>
}

function sliderValueLevel(duty: number) { return duty === 100 ? '高' : duty === 90 ? '中' : duty === 80 ? '低' : duty === 0 ? '关闭' : '自定义' }
