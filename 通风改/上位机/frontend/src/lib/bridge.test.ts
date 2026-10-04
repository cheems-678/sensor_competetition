import { afterEach, describe, expect, it, vi } from 'vitest'
import { connectBridge } from './bridge'
import type { DesktopAPI } from './types'

afterEach(() => {
  window.pywebview = undefined
  window.history.replaceState(null, '', '/')
})

function completeAPI(): DesktopAPI {
  return {
    refresh_ports: vi.fn().mockResolvedValue({ accepted: true }),
    connect: vi.fn().mockResolvedValue({ accepted: true }),
    disconnect: vi.fn().mockResolvedValue({ accepted: true }),
    read_once: vi.fn().mockResolvedValue({ accepted: true }),
    set_fan: vi.fn().mockResolvedValue({ accepted: true }),
    set_window: vi.fn().mockResolvedValue({ accepted: true }),
    get_snapshot: vi.fn(),
  }
}

describe('pywebview injection readiness', () => {
  it('waits for the complete API when pywebview initially exposes an empty object', async () => {
    window.history.replaceState(null, '', '/?desktop=1')
    window.pywebview = { api: {} as DesktopAPI }
    let resolved = false
    const pending = connectBridge().then(api => { resolved = true; return api.refresh_ports() })
    await Promise.resolve()
    expect(resolved).toBe(false)
    const api = completeAPI()
    window.pywebview.api = api
    expect(api.refresh_ports).not.toHaveBeenCalled()
    window.dispatchEvent(new Event('pywebviewready'))
    await expect(pending).resolves.toEqual({ accepted: true })
    expect(api.refresh_ports).toHaveBeenCalledTimes(1)
  })

  it('waits for partial native injection in a browser URL instead of selecting demo', async () => {
    window.pywebview = { api: {} as DesktopAPI }
    let resolved = false
    const pending = connectBridge().then(api => { resolved = true; return api })
    window.dispatchEvent(new Event('pywebviewready'))
    await Promise.resolve()
    expect(resolved).toBe(false)
    const api = completeAPI()
    window.pywebview.api = api
    window.dispatchEvent(new Event('pywebviewready'))
    await expect(pending).resolves.toBe(api)
  })

  it('uses an already complete native API without requiring a repeated ready event', async () => {
    const api = completeAPI()
    window.pywebview = { api }
    await expect(connectBridge()).resolves.toBe(api)
  })

  it('rejects an ordinary browser without silently simulating the device', async () => {
    await expect(connectBridge()).rejects.toThrow('请打开智能通风系统_正式版.exe 使用真实串口')
  })

  it('enables browser simulation only for an explicit preview parameter', async () => {
    window.history.replaceState(null, '', '/?preview=1')
    const api = await connectBridge()
    const snapshot = await api.get_snapshot()
    expect(snapshot.demo).toBe(true)
    expect(snapshot.ports).toEqual(['DEMO · 模拟控制室'])
  })
})
