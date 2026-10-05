import { act, cleanup, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { RainIndicator, RainMonitor, weatherState } from './rain-weather'

afterEach(() => { cleanup(); vi.restoreAllMocks(); vi.unstubAllGlobals() })

describe('rain weather based on real master telemetry', () => {
  it('accepts only connected master 0/1 readings and keeps invalid and historical states unknown', () => {
    expect(weatherState({ state: 0, source: 'master' }, true)).toBe('dry')
    expect(weatherState({ state: 1, source: 'master' }, true)).toBe('rain')
    for (const state of [0, 1, null] as const) {
      expect(weatherState({ state, source: 'master' }, false)).toBe('unknown')
      expect(weatherState({ state, source: null }, true)).toBe('unknown')
    }
    expect(weatherState({ state: null, source: 'master' }, true)).toBe('unknown')
    expect(weatherState(undefined, true)).toBe('unknown')
  })

  it('shows a sun only for dry weather, rain marks for wet weather, and a grey unknown icon otherwise', () => {
    const { rerender } = render(<RainIndicator state="dry" />)
    expect(screen.getByRole('img', { name: '太阳：无雨' })).toBeVisible()
    expect(screen.getByRole('status', { name: '总览雨滴状态' })).toHaveTextContent('无雨')
    rerender(<RainIndicator state="rain" />)
    expect(screen.getByRole('img', { name: '云朵与雨滴：有雨' })).toBeVisible()
    expect(document.querySelectorAll('.rain-mark')).toHaveLength(8)
    rerender(<RainIndicator state="unknown" />)
    expect(screen.getByRole('img', { name: '灰色云朵：状态未知' })).toBeVisible()
    expect(screen.queryByRole('img', { name: '太阳：无雨' })).not.toBeInTheDocument()
  })

  it('pauses motion without freezing live state or losing the pause choice across page changes', () => {
    const { rerender } = render(<RainMonitor state="rain" active updatedAt="12:34:56" />)
    const scene = screen.getByLabelText('雨滴状态动画')
    expect(scene).toHaveAttribute('data-running', 'true')
    expect(document.querySelectorAll('.rain-drop')).toHaveLength(16)
    expect(screen.getByText('12:34:56')).toBeVisible()
    fireEvent.click(screen.getByRole('button', { name: '暂停动画' }))
    expect(scene).toHaveAttribute('data-running', 'false')
    rerender(<RainMonitor state="dry" active={false} updatedAt="12:35:00" />)
    rerender(<RainMonitor state="dry" active updatedAt="12:35:00" />)
    expect(screen.getByRole('status', { name: '雨滴监测状态' })).toHaveTextContent('无雨')
    expect(screen.getByText('12:35:00')).toBeVisible()
    expect(scene).toHaveAttribute('data-running', 'false')
    fireEvent.click(screen.getByRole('button', { name: '继续动画' }))
    expect(scene).toHaveAttribute('data-running', 'true')
    rerender(<RainMonitor state="unknown" active updatedAt={null} />)
    expect(scene).toHaveAttribute('data-running', 'false')
  })

  it('stops on inactive pages, hidden documents and offscreen scenes, and releases visibility observers', () => {
    let intersectionChanged: (entries: { isIntersecting: boolean }[]) => void = () => {}
    const disconnect = vi.fn()
    vi.stubGlobal('IntersectionObserver', class {
      constructor(callback: typeof intersectionChanged) { intersectionChanged = callback }
      observe() {}; disconnect = disconnect
    })
    const { rerender, unmount } = render(<RainMonitor state="rain" active updatedAt={null} />)
    const scene = screen.getByLabelText('雨滴状态动画')
    act(() => intersectionChanged([{ isIntersecting: false }]))
    expect(scene).toHaveAttribute('data-running', 'false')
    act(() => intersectionChanged([{ isIntersecting: true }]))
    expect(scene).toHaveAttribute('data-running', 'true')
    const hidden = vi.spyOn(document, 'hidden', 'get').mockReturnValue(true)
    fireEvent(document, new Event('visibilitychange'))
    expect(scene).toHaveAttribute('data-running', 'false')
    hidden.mockReturnValue(false); fireEvent(document, new Event('visibilitychange'))
    expect(scene).toHaveAttribute('data-running', 'true')
    rerender(<RainMonitor state="rain" active={false} updatedAt={null} />)
    expect(scene).toHaveAttribute('data-running', 'false')
    unmount(); expect(disconnect).toHaveBeenCalledOnce()
  })

  it('starts paused when reduced motion is preferred and can be resumed explicitly', () => {
    vi.stubGlobal('matchMedia', () => ({ matches: true }))
    render(<RainMonitor state="dry" active updatedAt={null} />)
    expect(screen.getByLabelText('雨滴状态动画')).toHaveAttribute('data-running', 'false')
    fireEvent.click(screen.getByRole('button', { name: '继续动画' }))
    expect(screen.getByLabelText('雨滴状态动画')).toHaveAttribute('data-running', 'true')
  })
})
