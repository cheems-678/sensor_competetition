import { act, cleanup, renderHook } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { initialSnapshot, type DesktopAPI } from './types'
import { mergeLogs, useMonitor } from './use-monitor'

afterEach(() => { cleanup(); vi.useRealTimers() })

describe('snapshot polling', () => {
  it('retains the whole session without repeated log entries', () => {
    const original = [{ id: 1, text: 'TX first' }]
    const next = mergeLogs(original, [{ id: 1, text: 'TX first' }, { id: 2, text: 'RX second' }, { id: 2, text: 'RX second' }])
    expect(next).toEqual([{ id: 1, text: 'TX first' }, { id: 2, text: 'RX second' }])
    expect(mergeLogs(next, [{ id: 2, text: 'RX second' }])).toBe(next)
  })

  it('polls after completion without overlapping requests and cancels on unmount', async () => {
    vi.useFakeTimers()
    const state = { ...initialSnapshot(), logs: [{ id: 7, text: 'RX frame' }], last_log_id: 7 }
    let finish: ((value: typeof state) => void) | undefined
    const api = {
      refresh_ports: vi.fn().mockResolvedValue({ accepted: true }),
      get_snapshot: vi.fn().mockImplementationOnce(() => new Promise(resolve => { finish = resolve })).mockResolvedValue({ ...state, logs: [] }),
    } as unknown as DesktopAPI
    const { result, unmount } = renderHook(() => useMonitor(api))
    await act(async () => { await Promise.resolve() })
    expect(api.get_snapshot).toHaveBeenCalledExactlyOnceWith(0)
    await act(async () => { await vi.advanceTimersByTimeAsync(500) })
    expect(api.get_snapshot).toHaveBeenCalledTimes(1)
    await act(async () => { finish!(state); await Promise.resolve() })
    expect(result.current.logs).toHaveLength(1)
    await act(async () => { await vi.advanceTimersByTimeAsync(100) })
    expect(api.get_snapshot).toHaveBeenLastCalledWith(7)
    expect(result.current.logs).toHaveLength(1)
    unmount()
    await act(async () => { await vi.advanceTimersByTimeAsync(1000) })
    expect(api.get_snapshot).toHaveBeenCalledTimes(2)
  })
})
