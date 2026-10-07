import { act, cleanup, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { AcousticModel } from './acoustic-model'
import { acousticReading } from '../lib/acoustic'

afterEach(() => { cleanup(); vi.restoreAllMocks(); vi.unstubAllGlobals() })
const readings = ['0', '20', '30', '40', '50'].map(value => acousticReading(value, 0, true))

function environment() {
  let id = 0
  const frames = new Map<number, FrameRequestCallback>()
  const context = { setTransform: vi.fn(), clearRect: vi.fn(), createRadialGradient: () => ({ addColorStop: vi.fn() }),
    fillRect: vi.fn(), beginPath: vi.fn(), moveTo: vi.fn(), lineTo: vi.fn(), closePath: vi.fn(), fill: vi.fn(), stroke: vi.fn(), setLineDash: vi.fn() }
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(context as unknown as CanvasRenderingContext2D)
  vi.spyOn(HTMLCanvasElement.prototype, 'getBoundingClientRect').mockReturnValue({ width: 800, height: 400 } as DOMRect)
  vi.stubGlobal('requestAnimationFrame', (callback: FrameRequestCallback) => { frames.set(++id, callback); return id })
  vi.stubGlobal('cancelAnimationFrame', (frame: number) => frames.delete(frame))
  const flush = () => act(() => { for (const [key, callback] of [...frames]) { frames.delete(key); callback(0) } })
  const points = () => Array.from(document.querySelectorAll('.acoustic-point'), node => node.getAttribute('style'))
  return { context, frames, flush, points }
}

describe('sound model camera and alert motion', () => {
  it('draws once while idle; preserves camera across hidden pages and reset restores all points', () => {
    const drawing = environment()
    const { rerender, unmount } = render(<AcousticModel readings={readings} active />)
    drawing.flush()
    const initial = drawing.points()
    expect(drawing.frames.size).toBe(0)
    const canvas = screen.getByRole('img')
    fireEvent.keyDown(canvas, { key: 'ArrowRight' }); drawing.flush()
    expect(drawing.points()).not.toEqual(initial)
    const rotated = drawing.points()
    rerender(<AcousticModel readings={readings} active={false} />)
    const paints = drawing.context.clearRect.mock.calls.length
    fireEvent(window, new Event('resize')); drawing.flush()
    expect(drawing.context.clearRect).toHaveBeenCalledTimes(paints)
    expect(document.querySelector('.acoustic-model-stage')).toHaveAttribute('data-running', 'false')
    rerender(<AcousticModel readings={readings} active />); drawing.flush()
    expect(drawing.points()).toEqual(rotated)
    fireEvent.click(screen.getByRole('button', { name: '声学视角复位' })); drawing.flush()
    expect(drawing.points()).toEqual(initial)
    unmount()
    expect(drawing.frames.size).toBe(0)
  })
  it('supports pointer capture, wheel zoom, DPI sizing, and changes every marker with the same camera', () => {
    const drawing = environment()
    render(<AcousticModel readings={readings} active />)
    drawing.flush()
    const canvas = screen.getByRole('img') as HTMLCanvasElement
    canvas.setPointerCapture = vi.fn(); canvas.hasPointerCapture = () => true; canvas.releasePointerCapture = vi.fn()
    const initial = drawing.points()
    const pointer = (type: string, x: number) => {
      const event = new Event(type)
      Object.defineProperties(event, { pointerId: { value: 7 }, isPrimary: { value: true }, button: { value: 0 }, clientX: { value: x }, clientY: { value: 10 } })
      fireEvent(canvas, event)
    }
    pointer('pointerdown', 20); pointer('pointermove', 100); drawing.flush()
    expect(canvas.setPointerCapture).toHaveBeenCalledWith(7)
    expect(drawing.points()).not.toEqual(initial)
    pointer('pointercancel', 100)
    expect(canvas.releasePointerCapture).toHaveBeenCalledWith(7)
    expect(canvas.dataset.dragging).toBeUndefined()
    const rotated = drawing.points()
    vi.stubGlobal('devicePixelRatio', 1.5)
    fireEvent.wheel(canvas, { deltaY: -100 }); drawing.flush()
    expect(drawing.points()).not.toEqual(rotated)
    expect(canvas.width).toBe(1200); expect(canvas.height).toBe(600)
    expect(drawing.context.setTransform).toHaveBeenLastCalledWith(1.5, 0, 0, 1.5, 0, 0)
    expect(document.querySelectorAll('.acoustic-point')).toHaveLength(5)
  })
  it('stops blinking when hidden/offscreen and honours live reduced-motion changes without losing red state', () => {
    environment()
    let intersection = (_entries: { isIntersecting: boolean }[]) => {}
    const disconnect = vi.fn()
    vi.stubGlobal('IntersectionObserver', class {
      constructor(callback: typeof intersection) { intersection = callback }
      observe() {}; disconnect = disconnect
    })
    let preference = () => {}
    const media = { matches: false, addEventListener: (_event: string, callback: () => void) => { preference = callback }, removeEventListener: vi.fn() }
    vi.stubGlobal('matchMedia', () => media)
    const { unmount } = render(<AcousticModel readings={readings} active />)
    const stage = document.querySelector('.acoustic-model-stage')!
    expect(stage).toHaveAttribute('data-running', 'true')
    act(() => intersection([{ isIntersecting: false }]))
    expect(stage).toHaveAttribute('data-running', 'false')
    act(() => intersection([{ isIntersecting: true }]))
    expect(stage).toHaveAttribute('data-running', 'true')
    media.matches = true; act(() => preference())
    expect(stage).toHaveAttribute('data-running', 'false')
    expect(document.querySelectorAll('.acoustic-point.abnormal')).toHaveLength(4)
    media.matches = false; act(() => preference())
    const hidden = vi.spyOn(document, 'hidden', 'get').mockReturnValue(true)
    fireEvent(document, new Event('visibilitychange'))
    expect(stage).toHaveAttribute('data-running', 'false')
    hidden.mockReturnValue(false); fireEvent(document, new Event('visibilitychange'))
    expect(stage).toHaveAttribute('data-running', 'true')
    unmount()
    expect(disconnect).toHaveBeenCalledOnce()
    expect(media.removeEventListener).toHaveBeenCalled()
  })
})
