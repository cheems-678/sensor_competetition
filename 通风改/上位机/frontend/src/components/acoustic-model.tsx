import { useEffect, useRef, useState } from 'react'
import { Box, Minus, Plus, RotateCcw } from 'lucide-react'
import { Button } from './button'
import { adjustGranaryView, defaultGranaryView, drawAcousticGranary, type GranaryView } from '../lib/granary-model'
import { projectAcousticSensors, type AcousticReading } from '../lib/acoustic'
import { useVisualMotion } from '../lib/use-visual-motion'

export function AcousticModel({ readings, active }: { readings: AcousticReading[]; active: boolean }) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const view = useRef(defaultGranaryView())
  const scheduleRef = useRef(() => {})
  const [unavailable, setUnavailable] = useState(false)
  const [size, setSize] = useState({ width: 600, height: 380 })
  const [camera, setCamera] = useState(view.current)
  const motion = useVisualMotion(active)
  const change = (next: GranaryView) => { view.current = next; setCamera(next); scheduleRef.current() }
  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas || !active) return
    let context: CanvasRenderingContext2D | null = null
    try { context = canvas.getContext('2d') } catch { /* Keep the reading/state cards available. */ }
    if (!context) { setUnavailable(true); return }
    setUnavailable(false)
    let frame = 0
    let drag: { id: number; x: number; y: number } | null = null
    const paint = () => {
      frame = 0
      if (document.hidden) return
      const bounds = canvas.getBoundingClientRect()
      if (!bounds.width || !bounds.height) return
      const ratio = window.devicePixelRatio || 1
      canvas.width = Math.round(bounds.width * ratio); canvas.height = Math.round(bounds.height * ratio)
      context.setTransform(ratio, 0, 0, ratio, 0, 0)
      drawAcousticGranary(context, bounds.width, bounds.height, view.current)
      setSize(current => current.width === bounds.width && current.height === bounds.height ? current : { width: bounds.width, height: bounds.height })
    }
    const schedule = () => { if (!frame && !document.hidden) frame = requestAnimationFrame(paint) }
    scheduleRef.current = schedule
    const down = (event: PointerEvent) => {
      if (!event.isPrimary || event.button !== 0) return
      drag = { id: event.pointerId, x: event.clientX, y: event.clientY }
      canvas.setPointerCapture(event.pointerId); canvas.focus({ preventScroll: true }); canvas.dataset.dragging = 'true'
    }
    const move = (event: PointerEvent) => {
      if (!drag || drag.id !== event.pointerId) return
      change(adjustGranaryView(view.current, (event.clientX - drag.x) * .008, (event.clientY - drag.y) * .005))
      drag.x = event.clientX; drag.y = event.clientY
    }
    const end = (event: PointerEvent) => {
      if (!drag || drag.id !== event.pointerId) return
      drag = null; delete canvas.dataset.dragging
      if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId)
    }
    const wheel = (event: WheelEvent) => { event.preventDefault(); change(adjustGranaryView(view.current, 0, 0, event.deltaY > 0 ? .92 : 1.08)) }
    const key = (event: KeyboardEvent) => {
      const actions: Record<string, () => GranaryView> = {
        ArrowLeft: () => adjustGranaryView(view.current, -.12), ArrowRight: () => adjustGranaryView(view.current, .12),
        ArrowUp: () => adjustGranaryView(view.current, 0, -.08), ArrowDown: () => adjustGranaryView(view.current, 0, .08),
        '+': () => adjustGranaryView(view.current, 0, 0, 1.1), '=': () => adjustGranaryView(view.current, 0, 0, 1.1),
        '-': () => adjustGranaryView(view.current, 0, 0, .9), Home: defaultGranaryView,
      }
      if (actions[event.key]) { event.preventDefault(); change(actions[event.key]()) }
    }
    const visibility = () => { cancelAnimationFrame(frame); frame = 0; if (!document.hidden) schedule() }
    const observer = typeof ResizeObserver !== 'undefined' ? new ResizeObserver(schedule) : null
    observer?.observe(canvas)
    document.addEventListener('visibilitychange', visibility); window.addEventListener('resize', schedule)
    canvas.addEventListener('pointerdown', down); canvas.addEventListener('pointermove', move)
    canvas.addEventListener('pointerup', end); canvas.addEventListener('pointercancel', end); canvas.addEventListener('lostpointercapture', end)
    canvas.addEventListener('wheel', wheel, { passive: false }); canvas.addEventListener('keydown', key)
    schedule()
    return () => {
      observer?.disconnect(); cancelAnimationFrame(frame); scheduleRef.current = () => {}
      document.removeEventListener('visibilitychange', visibility); window.removeEventListener('resize', schedule)
      canvas.removeEventListener('pointerdown', down); canvas.removeEventListener('pointermove', move)
      canvas.removeEventListener('pointerup', end); canvas.removeEventListener('pointercancel', end); canvas.removeEventListener('lostpointercapture', end)
      canvas.removeEventListener('wheel', wheel); canvas.removeEventListener('keydown', key)
      if (drag && canvas.hasPointerCapture(drag.id)) canvas.releasePointerCapture(drag.id)
      delete canvas.dataset.dragging
    }
  }, [active])
  const sensors = projectAcousticSensors(size.width, size.height, camera)
  return <section className="panel acoustic-model" aria-labelledby="acoustic-model-title">
    <div className="granary-heading"><div className="heading-name"><Box size={18} /><h2 id="acoustic-model-title">粮仓声音点位</h2></div>
      <div className="granary-controls">
        <Button variant="ghost" size="icon" aria-label="缩小声学模型" disabled={unavailable} onClick={() => change(adjustGranaryView(view.current, 0, 0, .9))}><Minus size={16} /></Button>
        <Button variant="ghost" size="icon" aria-label="放大声学模型" disabled={unavailable} onClick={() => change(adjustGranaryView(view.current, 0, 0, 1.1))}><Plus size={16} /></Button>
        <Button variant="outline" size="small" disabled={unavailable} onClick={() => change(defaultGranaryView())}><RotateCcw size={14} />声学视角复位</Button>
      </div>
    </div>
    <div ref={motion.ref as React.RefObject<HTMLDivElement>} className="acoustic-model-stage" data-running={motion.running && !unavailable}>
      <canvas ref={canvasRef} className="acoustic-canvas" role="img" tabIndex={0} aria-label="透明粮仓声音三维示意：五个内壁点位同高、间隔72度" />
      {!unavailable && <div className="acoustic-points" aria-label="五路声音点位">{sensors.map((sensor, index) => {
        const reading = readings[index]
        return <div className={`acoustic-point ${reading.state}`} key={sensor.pin} data-channel={sensor.channel} data-pin={sensor.pin} data-state={reading.state}
          style={{ left: `${sensor.screen[0] / size.width * 100}%`, top: `${sensor.screen[1] / size.height * 100}%` }}
          aria-label={`声音 ${sensor.channel} ${sensor.pin}：${reading.label}`}>
          <span className="acoustic-point-light"><b>{sensor.channel}</b></span><span className={`acoustic-point-pin ${sensor.screen[0] < size.width * .5 ? 'pin-left' : 'pin-right'}`}>{sensor.pin}</span>
        </div>
      })}</div>}
      {unavailable && <p className="acoustic-fallback" role="status">当前环境无法显示三维模型，五路读数与状态仍可使用。</p>}
      <span className="acoustic-ring-note">内壁环向布点 · 72° 等间隔</span>
    </div>
    <div className="acoustic-legend"><span><i className="normal" />未超阈值</span><span><i className="abnormal" />幅度超限</span><span><i className="unavailable" />数据不可用 / 待设置</span></div>
  </section>
}
