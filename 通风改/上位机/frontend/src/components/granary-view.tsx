import { useEffect, useRef, useState } from 'react'
import { Box, Minus, Pause, Play, Plus, RotateCcw } from 'lucide-react'
import { Button } from './button'
import { HelpDetails } from './help-details'
import { adjustGranaryView, defaultGranaryView, drawGranary, type GranaryView as View } from '../lib/granary-model'
import { simulatedTemperature, temperatureColor, temperatureGradient, temperatureStops } from '../lib/simulated-temperature'
export function GranaryView({ active: pageActive = true }: { active?: boolean }) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const view = useRef(defaultGranaryView())
  const render = useRef<() => void>(() => {})
  const [unavailable, setUnavailable] = useState(false)
  const [paused, setPaused] = useState(() => window.matchMedia?.('(prefers-reduced-motion: reduce)').matches ?? false)
  const elapsed = useRef(0)
  const [temperatures, setTemperatures] = useState(() => ({ lower: simulatedTemperature(-1.1, 0), upper: simulatedTemperature(1.02, 0) }))
  const change = (next: View) => { view.current = next; render.current() }
  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    if (!pageActive) return
    let context: CanvasRenderingContext2D | null = null
    try { context = canvas.getContext('2d') } catch { /* A text fallback keeps the controls usable. */ }
    if (!context) { setUnavailable(true); return }
    let frame = 0, dirty = true, lastDraw = -Infinity, lastLabels = -Infinity, previous = performance.now()
    let inViewport = true
    const active = () => !paused && !document.hidden && inViewport
    const paint = (now: number) => {
      frame = 0
      if (active()) elapsed.current += Math.max(0, Math.min(.5, (now - previous) / 1000))
      previous = now
      if (dirty || (active() && now - lastDraw >= 250)) {
        dirty = false; lastDraw = now
        const bounds = canvas.getBoundingClientRect()
        if (bounds.width && bounds.height) {
          const ratio = window.devicePixelRatio || 1
          const width = Math.round(bounds.width * ratio), height = Math.round(bounds.height * ratio)
          if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height }
          context.setTransform(ratio, 0, 0, ratio, 0, 0)
          drawGranary(context, bounds.width, bounds.height, view.current, elapsed.current)
        }
        if (now - lastLabels >= 1000) {
          lastLabels = now
          setTemperatures({ lower: simulatedTemperature(-1.1, elapsed.current), upper: simulatedTemperature(1.02, elapsed.current) })
        }
      }
      if (active()) frame = requestAnimationFrame(paint)
    }
    const schedule = () => {
      dirty = true
      if (!frame) frame = requestAnimationFrame(paint)
    }
    render.current = schedule
    let drag: { id: number; x: number; y: number } | null = null
    const down = (event: PointerEvent) => {
      if (!event.isPrimary || event.button !== 0) return
      drag = { id: event.pointerId, x: event.clientX, y: event.clientY }
      canvas.setPointerCapture(event.pointerId); canvas.focus({ preventScroll: true })
      canvas.dataset.dragging = 'true'
    }
    const move = (event: PointerEvent) => {
      if (!drag || drag.id !== event.pointerId) return
      view.current = adjustGranaryView(view.current, (event.clientX - drag.x) * .008, (event.clientY - drag.y) * .005)
      drag.x = event.clientX; drag.y = event.clientY; schedule()
    }
    const end = (event: PointerEvent) => {
      if (!drag || drag.id !== event.pointerId) return
      drag = null; delete canvas.dataset.dragging
      if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId)
    }
    const wheel = (event: WheelEvent) => {
      event.preventDefault()
      view.current = adjustGranaryView(view.current, 0, 0, event.deltaY > 0 ? .92 : 1.08); schedule()
    }
    const key = (event: KeyboardEvent) => {
      const actions: Record<string, () => View> = {
        ArrowLeft: () => adjustGranaryView(view.current, -.12), ArrowRight: () => adjustGranaryView(view.current, .12),
        ArrowUp: () => adjustGranaryView(view.current, 0, -.08), ArrowDown: () => adjustGranaryView(view.current, 0, .08),
        '+': () => adjustGranaryView(view.current, 0, 0, 1.1), '=': () => adjustGranaryView(view.current, 0, 0, 1.1),
        '-': () => adjustGranaryView(view.current, 0, 0, .9), Home: defaultGranaryView,
      }
      if (actions[event.key]) { event.preventDefault(); view.current = actions[event.key](); schedule() }
    }
    const observer = typeof ResizeObserver !== 'undefined' ? new ResizeObserver(schedule) : null
    observer?.observe(canvas)
    const visibility = () => { previous = performance.now(); if (frame) cancelAnimationFrame(frame); frame = 0; if (!document.hidden) schedule() }
    const intersection = typeof IntersectionObserver !== 'undefined' ? new IntersectionObserver(entries => {
      inViewport = entries[0]?.isIntersecting ?? true; previous = performance.now()
      if (frame) cancelAnimationFrame(frame); frame = 0
      if (inViewport) schedule()
    }) : null
    intersection?.observe(canvas)
    document.addEventListener('visibilitychange', visibility)
    window.addEventListener('resize', schedule)
    canvas.addEventListener('pointerdown', down); canvas.addEventListener('pointermove', move)
    canvas.addEventListener('pointerup', end); canvas.addEventListener('pointercancel', end); canvas.addEventListener('lostpointercapture', end)
    canvas.addEventListener('wheel', wheel, { passive: false }); canvas.addEventListener('keydown', key)
    schedule()
    return () => {
      observer?.disconnect(); intersection?.disconnect(); document.removeEventListener('visibilitychange', visibility); window.removeEventListener('resize', schedule)
      canvas.removeEventListener('pointerdown', down); canvas.removeEventListener('pointermove', move)
      canvas.removeEventListener('pointerup', end); canvas.removeEventListener('pointercancel', end); canvas.removeEventListener('lostpointercapture', end)
      canvas.removeEventListener('wheel', wheel); canvas.removeEventListener('keydown', key)
      if (drag && canvas.hasPointerCapture(drag.id)) canvas.releasePointerCapture(drag.id)
      cancelAnimationFrame(frame); render.current = () => {}
    }
  }, [paused, pageActive])
  return <section className="panel granary-panel" aria-labelledby="granary-title">
    <div className="granary-heading">
      <div className="heading-name"><Box size={18} strokeWidth={1.6} /><h2 id="granary-title">粮仓三维视图</h2></div>
      <div className="granary-controls">
        <Button variant="ghost" size="small" disabled={unavailable} onClick={() => setPaused(value => !value)}>{paused ? <Play size={14} /> : <Pause size={14} />}{paused ? '继续模拟' : '暂停模拟'}</Button>
        <Button variant="ghost" size="icon" aria-label="缩小粮仓模型" disabled={unavailable} onClick={() => change(adjustGranaryView(view.current, 0, 0, .9))}><Minus size={16} /></Button>
        <Button variant="ghost" size="icon" aria-label="放大粮仓模型" disabled={unavailable} onClick={() => change(adjustGranaryView(view.current, 0, 0, 1.1))}><Plus size={16} /></Button>
        <Button variant="outline" size="small" disabled={unavailable} onClick={() => change(defaultGranaryView())}><RotateCcw size={14} />视角复位</Button>
      </div>
    </div>
    <div className="granary-stage">
      <canvas ref={canvasRef} className="granary-canvas" tabIndex={0} role="img" aria-label="粮仓三维示意模型：圆筒仓体、锥形仓顶与固定内部剖面" aria-describedby="granary-help" />
      {unavailable && <p className="granary-fallback" role="status">当前环境无法显示三维模型，其他操作仍可使用。</p>}
      <div className="granary-caption">
        <div className="thermal-readings">
          {[['上层', temperatures.upper], ['下层', temperatures.lower]].map(([label, temperature]) => <div key={label}><span>{label}</span><b style={{ color: `rgb(${temperatureColor(Number(temperature)).join(',')})` }}>{Number(temperature).toFixed(1)}<small> °C</small></b></div>)}
        </div>
        <div className="temperature-spectrum" aria-label="模拟温度色谱，16℃蓝色到36℃橙色">
          <div className="spectrum-heading"><span>低温</span><span>温度色谱 / °C</span><span>高温</span></div>
          <div className="spectrum-bar" style={{ background: temperatureGradient }} />
          <div className="spectrum-stops">{temperatureStops.map(stop => <span key={stop.temperature}><b>{stop.temperature}°</b><small>{stop.label}</small></span>)}</div>
        </div>
      </div>
    </div>
    <HelpDetails id="granary-help"><p>拖拽旋转，滚轮缩放；键盘方向键旋转，+/− 缩放，Home 复位。</p><p>温度为视觉模拟，不是实测粮温；模型不表示实际粮位或传感器分布。</p></HelpDetails>
  </section>
}
