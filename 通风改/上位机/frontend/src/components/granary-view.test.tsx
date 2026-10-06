import { act, cleanup, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { GranaryView } from './granary-view'

afterEach(() => { cleanup(); vi.restoreAllMocks(); vi.unstubAllGlobals() })

function drawingEnvironment() {
  let id = 0
  const frames = new Map<number, FrameRequestCallback>()
  const gradient = { addColorStop: vi.fn() }
  const context = {
    setTransform: vi.fn(), clearRect: vi.fn(), createRadialGradient: () => gradient,
    fillRect: vi.fn(), beginPath: vi.fn(), moveTo: vi.fn(), lineTo: vi.fn(),
    stroke: vi.fn(), ellipse: vi.fn(), fill: vi.fn(), closePath: vi.fn(),
  }
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(context as unknown as CanvasRenderingContext2D)
  vi.spyOn(HTMLCanvasElement.prototype, 'getBoundingClientRect').mockReturnValue({ width: 600, height: 300 } as DOMRect)
  const disconnect = vi.fn(), observe = vi.fn()
  let resized: () => void = () => {}
  vi.stubGlobal('ResizeObserver', class {
    constructor(callback: () => void) { resized = callback }
    observe = observe; disconnect = disconnect
  })
  vi.stubGlobal('requestAnimationFrame', (callback: FrameRequestCallback) => { frames.set(++id, callback); return id })
  vi.stubGlobal('cancelAnimationFrame', (frame: number) => frames.delete(frame))
  const flush = (time = 0) => act(() => { for (const [key, callback] of [...frames]) { frames.delete(key); callback(time) } })
  const projectedPoints = () => context.moveTo.mock.calls.map(args => [...args])
  return { context, frames, disconnect, observe, flush, projectedPoints, resize: () => resized() }
}

describe('standalone granary model', () => {
  it('explicitly stops hidden-page drawing and preserves the camera and pause state on return', () => {
    const drawing = drawingEnvironment()
    const { rerender } = render(<GranaryView active />)
    drawing.flush()
    fireEvent.click(screen.getByRole('button', { name: '放大粮仓模型' }))
    fireEvent.click(screen.getByRole('button', { name: '暂停模拟' }))
    drawing.context.moveTo.mockClear(); drawing.flush()
    const zoomed = drawing.projectedPoints()
    const paints = drawing.context.clearRect.mock.calls.length
    rerender(<GranaryView active={false} />)
    fireEvent(window, new Event('resize'))
    drawing.flush(10000)
    expect(drawing.frames.size).toBe(0)
    expect(drawing.context.clearRect).toHaveBeenCalledTimes(paints)
    drawing.context.moveTo.mockClear()
    rerender(<GranaryView active />); drawing.flush(11000)
    expect(drawing.projectedPoints()).toEqual(zoomed)
    expect(screen.getByRole('button', { name: '继续模拟' })).toBeVisible()
    expect(drawing.frames.size).toBe(0)
  })

  it('throttles simulation drawing, freezes on pause, and restarts without changing the camera', () => {
    vi.spyOn(performance, 'now').mockReturnValue(0)
    const drawing = drawingEnvironment()
    render(<GranaryView />)
    drawing.flush(0)
    const initial = drawing.projectedPoints()
    expect(drawing.context.clearRect).toHaveBeenCalledTimes(1)
    drawing.flush(100)
    expect(drawing.context.clearRect).toHaveBeenCalledTimes(1)
    drawing.flush(250)
    expect(drawing.context.clearRect).toHaveBeenCalledTimes(2)
    fireEvent.click(screen.getByRole('button', { name: '暂停模拟' }))
    drawing.flush(500)
    const paints = drawing.context.clearRect.mock.calls.length
    expect(drawing.frames.size).toBe(0)
    expect(screen.getByRole('button', { name: '继续模拟' })).toBeVisible()
    drawing.flush(10000)
    expect(drawing.context.clearRect).toHaveBeenCalledTimes(paints)
    drawing.context.moveTo.mockClear()
    fireEvent.click(screen.getByRole('button', { name: '继续模拟' }))
    drawing.flush(10000)
    expect(drawing.projectedPoints()).toEqual(initial)
    expect(drawing.frames.size).toBe(1)
  })

  it('does not animate while the document is hidden or reduced motion is preferred', () => {
    const drawing = drawingEnvironment()
    vi.stubGlobal('matchMedia', () => ({ matches: true }))
    render(<GranaryView />)
    drawing.flush()
    expect(screen.getByRole('button', { name: '继续模拟' })).toBeVisible()
    expect(drawing.frames.size).toBe(0)
    fireEvent.click(screen.getByRole('button', { name: '继续模拟' }))
    drawing.flush()
    expect(drawing.frames.size).toBe(1)
    const hidden = vi.spyOn(document, 'hidden', 'get').mockReturnValue(true)
    fireEvent(document, new Event('visibilitychange'))
    expect(drawing.frames.size).toBe(0)
    hidden.mockReturnValue(false)
    fireEvent(document, new Event('visibilitychange'))
    drawing.flush()
    expect(drawing.frames.size).toBe(1)
  })

  it('stops animation when the model leaves the viewport and resumes when it returns', () => {
    const drawing = drawingEnvironment()
    let visibilityChanged: (entries: { isIntersecting: boolean }[]) => void = () => {}
    const disconnect = vi.fn()
    vi.stubGlobal('IntersectionObserver', class {
      constructor(callback: typeof visibilityChanged) { visibilityChanged = callback }
      observe() {}; disconnect = disconnect
    })
    const { unmount } = render(<GranaryView />)
    drawing.flush()
    expect(drawing.frames.size).toBe(1)
    act(() => visibilityChanged([{ isIntersecting: false }]))
    expect(drawing.frames.size).toBe(0)
    act(() => visibilityChanged([{ isIntersecting: true }]))
    drawing.flush()
    expect(drawing.frames.size).toBe(1)
    unmount()
    expect(disconnect).toHaveBeenCalledOnce()
    expect(drawing.frames.size).toBe(0)
  })

  it('draws without device data, supports zoom and keyboard rotation, and restores the original view', () => {
    const drawing = drawingEnvironment()
    render(<GranaryView />)
    const canvas = screen.getByRole('img', { name: /粮仓三维示意模型/ })
    drawing.flush()
    const initial = drawing.projectedPoints()
    expect(initial.length).toBeGreaterThan(100)
    expect(screen.getByText(/模型不表示实际粮位或传感器分布/)).not.toBeVisible()
    fireEvent.click(screen.getByText('说明'))
    expect(screen.getByText(/模型不表示实际粮位或传感器分布/)).toBeVisible()
    drawing.context.moveTo.mockClear()
    fireEvent.click(screen.getByRole('button', { name: '放大粮仓模型' }))
    drawing.flush()
    expect(drawing.projectedPoints()).not.toEqual(initial)
    drawing.context.moveTo.mockClear()
    fireEvent.click(screen.getByRole('button', { name: '视角复位' }))
    drawing.flush()
    expect(drawing.projectedPoints()).toEqual(initial)
    drawing.context.moveTo.mockClear()
    fireEvent.keyDown(canvas, { key: 'ArrowRight' })
    drawing.flush()
    expect(drawing.projectedPoints()).not.toEqual(initial)
    drawing.context.moveTo.mockClear()
    fireEvent.keyDown(canvas, { key: 'Home' })
    drawing.flush()
    expect(drawing.projectedPoints()).toEqual(initial)
    for (const args of [...drawing.context.moveTo.mock.calls, ...drawing.context.lineTo.mock.calls]) {
      expect(args.every(Number.isFinite)).toBe(true)
    }
  })

  it('captures a drag, sizes for DPI changes, and releases observers and scheduled frames on unmount', () => {
    const drawing = drawingEnvironment()
    const { unmount } = render(<GranaryView />)
    const canvas = screen.getByRole('img') as HTMLCanvasElement
    canvas.setPointerCapture = vi.fn(); canvas.hasPointerCapture = () => true; canvas.releasePointerCapture = vi.fn()
    drawing.flush()
    const initial = drawing.projectedPoints()
    drawing.context.moveTo.mockClear()
    function pointer(type: string, x: number) {
      const event = new Event(type)
      Object.defineProperties(event, { pointerId: { value: 7 }, isPrimary: { value: true }, button: { value: 0 }, clientX: { value: x }, clientY: { value: 10 } })
      fireEvent(canvas, event)
    }
    pointer('pointerdown', 20); pointer('pointermove', 80); drawing.flush()
    expect(canvas.setPointerCapture).toHaveBeenCalledWith(7)
    expect(drawing.projectedPoints()).not.toEqual(initial)
    pointer('pointercancel', 80)
    expect(canvas.releasePointerCapture).toHaveBeenCalledWith(7)
    expect(canvas.dataset.dragging).toBeUndefined()
    vi.stubGlobal('devicePixelRatio', 1.5)
    drawing.resize(); drawing.flush()
    expect(canvas.width).toBe(900); expect(canvas.height).toBe(450)
    expect(drawing.context.setTransform).toHaveBeenLastCalledWith(1.5, 0, 0, 1.5, 0, 0)
    fireEvent.wheel(canvas, { deltaY: 100 })
    expect(drawing.frames.size).toBe(1)
    unmount()
    expect(drawing.disconnect).toHaveBeenCalledOnce()
    expect(drawing.frames.size).toBe(0)
    fireEvent.wheel(canvas, { deltaY: 100 })
    expect(drawing.frames.size).toBe(0)
  })
})
