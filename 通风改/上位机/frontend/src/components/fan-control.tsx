import { useEffect, useRef, useState } from 'react'
import { ArrowUpRight, Fan } from 'lucide-react'
import type { FanState } from '../lib/types'
import { Button } from './button'
import { decimalDraft } from '../lib/duty'

const adjustmentKeys = new Set(['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Home', 'End', 'PageUp', 'PageDown'])

export function FanControl({ fan, enabled, submit }: { fan: FanState; enabled: boolean; submit: (channel: number, duty: number | string) => void }) {
  const [draft, setDraft] = useState(String(fan.duty))
  const draftRef = useRef(draft)
  const pointerActive = useRef(false)
  const keys = useRef(new Set<string>())
  const updateDraft = (value: string) => { draftRef.current = value; setDraft(value) }
  useEffect(() => {
    if (!enabled) { pointerActive.current = false; keys.current.clear() }
  }, [enabled])
  const number = decimalDraft(draft)
  const sliderValue = Number.isFinite(number) ? Math.max(0, Math.min(100, number)) : 0
  const commit = () => {
    if (!enabled) return
    const raw = draftRef.current
    const numeric = decimalDraft(raw)
    if (Number.isFinite(numeric) && numeric >= 0 && numeric <= 100) {
      const normalized = Math.floor(numeric + 0.5)
      updateDraft(String(normalized))
      submit(fan.channel, normalized)
    } else submit(fan.channel, raw)
  }
  return <section className="fan-control" aria-labelledby={`fan-title-${fan.channel}`}>
    <div className="fan-header">
      <div className="control-name"><Fan size={17} strokeWidth={1.7} /><h3 id={`fan-title-${fan.channel}`}>风机 {fan.channel}</h3><span className="pin">{fan.pin}</span></div>
      <span className="micro-label">PWM 占空比</span>
    </div>
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
