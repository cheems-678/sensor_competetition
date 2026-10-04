import { act, cleanup, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { App, Metric } from './App'
import { initialSnapshot, type DesktopAPI } from './lib/types'

afterEach(() => { cleanup(); vi.useRealTimers() })

function fakeAPI(state = initialSnapshot()): DesktopAPI {
  return {
    refresh_ports: vi.fn().mockResolvedValue({ accepted: true }),
    connect: vi.fn().mockResolvedValue({ accepted: true }), disconnect: vi.fn().mockResolvedValue({ accepted: true }),
    read_once: vi.fn().mockResolvedValue({ accepted: true }), set_fan: vi.fn().mockResolvedValue({ accepted: true }),
    set_window: vi.fn().mockResolvedValue({ accepted: true }), get_snapshot: vi.fn().mockImplementation(async () => structuredClone(state)),
  }
}

describe('existing controls and readable values', () => {
  it('keeps disconnected fan/read warnings available but disables window actions', async () => {
    const api = fakeAPI()
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    expect(screen.getByRole('button', { name: '读取单帧' })).toBeEnabled()
    expect(screen.getByRole('button', { name: '发送风机 1 占空比' })).toBeEnabled()
    expect(screen.getByRole('button', { name: '打开窗户' })).toBeDisabled()
    expect(screen.getByRole('button', { name: '关闭窗户' })).toBeDisabled()
    fireEvent.click(screen.getByRole('button', { name: '读取单帧' }))
    expect(api.read_once).toHaveBeenCalledTimes(1)
  })

  it('binds fan channels and window actions without treating bridge acceptance as ACK', async () => {
    const state = initialSnapshot()
    state.connected = true
    state.revision = 1
    state.controls.window_enabled = true
    const api = fakeAPI(state)
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    fireEvent.change(screen.getByRole('textbox', { name: '风机 2 占空比' }), { target: { value: '32' } })
    fireEvent.click(screen.getByRole('button', { name: '发送风机 2 占空比' }))
    expect(api.set_fan).toHaveBeenCalledExactlyOnceWith(2, 32)
    fireEvent.click(screen.getByRole('button', { name: '打开窗户' }))
    fireEvent.click(screen.getByRole('button', { name: '关闭窗户' }))
    expect(api.set_window).toHaveBeenNthCalledWith(1, 1)
    expect(api.set_window).toHaveBeenNthCalledWith(2, 0)
    expect(screen.queryByText(/已确认/)).not.toBeInTheDocument()
  })

  it('disables fan submissions and reads while a window action is busy', async () => {
    const state = initialSnapshot()
    state.window.busy = true
    state.revision = 1
    state.controls = { read_enabled: false, fan_enabled: false, window_enabled: false }
    render(<App api={fakeAPI(state)} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    expect(screen.getByRole('button', { name: '读取单帧' })).toBeDisabled()
    expect(screen.getByRole('button', { name: '发送风机 1 占空比' })).toBeDisabled()
    expect(screen.getByRole('button', { name: '发送风机 2 占空比' })).toBeDisabled()
    expect(screen.getByRole('slider', { name: '风机 1 占空比滑块' })).toBeEnabled()
  })

  it('refreshes a cleared port from latest choices but preserves custom drafts', async () => {
    vi.useFakeTimers()
    const state = initialSnapshot()
    state.port = 'COM1'
    state.ports = ['COM1']
    state.revision = 1
    const api = fakeAPI(state)
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    const port = screen.getByRole('combobox', { name: '串口端口' })
    expect(port).toHaveValue('COM1')
    fireEvent.change(port, { target: { value: '' } })
    fireEvent.click(screen.getByRole('button', { name: '刷新串口' }))
    state.ports = ['COM9']
    state.revision++
    await act(async () => { await vi.advanceTimersByTimeAsync(100) })
    expect(port).toHaveValue('COM9')
    fireEvent.change(port, { target: { value: 'COM42' } })
    fireEvent.click(screen.getByRole('button', { name: '刷新串口' }))
    state.ports = ['COM10']
    state.revision++
    await act(async () => { await vi.advanceTimersByTimeAsync(100) })
    expect(port).toHaveValue('COM42')
    vi.useRealTimers()
  })

  it('renders long numbers and placeholder readings without losing units', () => {
    const { rerender } = render(<Metric value="4294967294 Pa" label="绝对气压" unit="Pa" />)
    expect(screen.getByText('4294967294')).toBeVisible()
    expect(screen.getByText('Pa')).toBeVisible()
    rerender(<Metric value="--" label="绝对气压" unit="Pa" />)
    expect(screen.getByText('--')).toBeVisible()
    expect(screen.getByText('Pa')).toBeVisible()
    rerender(<Metric value="23.6 ℃" label="温度" unit="°C" />)
    expect(screen.getByText('23.6')).toBeVisible()
    expect(screen.getByText('℃')).toBeVisible()
    expect(screen.queryByText('°C')).not.toBeInTheDocument()
    rerender(<Metric value="47.8 %RH" label="湿度" unit="%" />)
    expect(screen.getByText('47.8')).toBeVisible()
    expect(screen.getByText('%RH')).toBeVisible()
    expect(screen.queryByText('%')).not.toBeInTheDocument()
  })

  it('shows COM32 in the full list with COM6 already selected and connects the chosen port', async () => {
    const state = initialSnapshot()
    state.port = 'COM6'
    state.ports = ['COM6', 'COM7', 'COM32']
    state.revision = 1
    const api = fakeAPI(state)
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    const port = screen.getByRole('combobox', { name: '串口端口' })
    expect(port).toHaveValue('COM6')
    fireEvent.click(screen.getByRole('button', { name: '展开串口列表' }))
    expect(screen.getAllByRole('option')).toHaveLength(3)
    expect(screen.getByRole('option', { name: 'COM32' })).toBeVisible()
    fireEvent.click(screen.getByRole('option', { name: 'COM32' }))
    expect(port).toHaveValue('COM32')
    expect(screen.queryByRole('listbox')).not.toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: '连接设备' }))
    expect(api.connect).toHaveBeenCalledExactlyOnceWith('COM32')
  })

  it('supports keyboard selection, Escape and outside dismissal without losing a custom port', async () => {
    const state = initialSnapshot()
    state.port = 'COM6'
    state.ports = ['COM6', 'COM7', 'COM32']
    state.revision = 1
    render(<App api={fakeAPI(state)} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    const port = screen.getByRole('combobox', { name: '串口端口' })
    fireEvent.keyDown(port, { key: 'ArrowDown' })
    fireEvent.keyDown(port, { key: 'ArrowDown' })
    fireEvent.keyDown(port, { key: 'ArrowDown' })
    fireEvent.keyDown(port, { key: 'Enter' })
    expect(port).toHaveValue('COM32')
    fireEvent.change(port, { target: { value: 'COM42' } })
    fireEvent.click(screen.getByRole('button', { name: '展开串口列表' }))
    fireEvent.keyDown(port, { key: 'Escape' })
    expect(screen.queryByRole('listbox')).not.toBeInTheDocument()
    expect(port).toHaveValue('COM42')
    fireEvent.click(screen.getByRole('button', { name: '展开串口列表' }))
    fireEvent.pointerDown(document.body)
    expect(screen.queryByRole('listbox')).not.toBeInTheDocument()
    expect(port).toHaveValue('COM42')
  })

  it('requires the formal EXE in an ordinary browser without inventing ports or readings', async () => {
    render(<App />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    expect(screen.getByRole('alert')).toHaveTextContent('请打开智能通风系统_正式版.exe 使用真实串口')
    expect(screen.getByRole('combobox', { name: '串口端口' })).toHaveValue('')
    expect(screen.getByRole('button', { name: '连接设备' })).toBeDisabled()
    expect(screen.queryByText(/演示模式/)).not.toBeInTheDocument()
    expect(screen.queryByText('24.6')).not.toBeInTheDocument()
  })
})
