import { act, cleanup, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { within } from '@testing-library/react'
import { App, Metric } from './App'
import { initialSnapshot, type DesktopAPI } from './lib/types'
import { BrowserDemo } from './lib/demo'

afterEach(() => { cleanup(); vi.useRealTimers(); vi.restoreAllMocks(); vi.unstubAllGlobals() })

const navigate = (title: string) => fireEvent.click(screen.getByRole('button', { name: `切换到${title}` }))

function fakeAPI(state = initialSnapshot()): DesktopAPI {
  return {
    refresh_ports: vi.fn().mockResolvedValue({ accepted: true }),
    connect: vi.fn().mockResolvedValue({ accepted: true }), disconnect: vi.fn().mockResolvedValue({ accepted: true }),
    read_once: vi.fn().mockResolvedValue({ accepted: true }), set_fan: vi.fn().mockResolvedValue({ accepted: true }),
    set_window: vi.fn().mockResolvedValue({ accepted: true }), get_snapshot: vi.fn().mockImplementation(async () => structuredClone(state)),
  }
}

describe('existing controls and readable values', () => {
  it('shows MQ-2 zero and voltage readings, then clears invalid or disconnected data without commands', async () => {
    vi.useFakeTimers()
    const state = initialSnapshot(); state.connected = true; state.revision = 1
    state.telemetry.mq2 = { valid: true, raw: 0, pa7_mv: 1234, ao_mv: 2468, age_ms: 50 }
    const api = fakeAPI(state)
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    navigate('烟雾监测')
    const page = document.querySelector('[data-page="smoke"]') as HTMLElement
    expect(page).toBeVisible()
    expect(within(page).getByText('ADC 原始值').parentElement).toHaveTextContent('0计数')
    expect(within(page).getByText('PA7 输入电压').parentElement).toHaveTextContent('1.234V')
    expect(within(page).getByText('AO 还原电压').parentElement).toHaveTextContent('2.468V')
    expect(within(page).getByText('已知采样年龄').parentElement).toHaveTextContent('50ms')
    expect(within(page).getByRole('status', { name: '烟雾采样状态' })).toHaveTextContent('有效模拟量')
    expect(within(page).getByLabelText('相对烟雾指数')).toHaveTextContent('46')
    expect(page).toHaveTextContent('未标定')
    state.telemetry.mq2.valid = false; state.revision++
    await act(async () => { await vi.advanceTimersByTimeAsync(100) })
    expect(within(page).getByRole('status', { name: '烟雾采样状态' })).toHaveTextContent('数据不可用')
    expect(within(page).getAllByText('--')).toHaveLength(5)
    state.telemetry.mq2.valid = true; state.connected = false; state.revision++
    await act(async () => { await vi.advanceTimersByTimeAsync(100) })
    expect(within(page).getAllByText('--')).toHaveLength(5)
    for (const method of [api.connect, api.disconnect, api.read_once, api.set_fan, api.set_window]) expect(method).not.toHaveBeenCalled()
  })
  it('keeps overview, animation and environment rain synchronized without submitting commands', async () => {
    vi.useFakeTimers()
    const state = initialSnapshot(); state.connected = true; state.revision = 1
    const api = fakeAPI(state)
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    expect(screen.getAllByRole('button', { name: /^切换到/ })).toHaveLength(8)
    expect(document.querySelector('.page-index')).toHaveTextContent('01 / 08')
    for (const [value, text] of [[0, '无雨'], [1, '有雨'], [null, '状态未知']] as const) {
      state.telemetry.rain = { state: value, source: 'master' }
      state.telemetry.slave_link = '从机链路：离线'; state.revision++
      await act(async () => { await vi.advanceTimersByTimeAsync(100) })
      navigate('粮仓总览')
      expect(screen.getByRole('status', { name: '总览雨滴状态' })).toHaveTextContent(text)
      navigate('雨滴监测')
      expect(screen.getByRole('status', { name: '雨滴监测状态' })).toHaveTextContent(text)
      navigate('环境监测')
      expect(screen.getByRole('status', { name: '主机雨滴状态' })).toHaveTextContent(value === null ? '--' : text)
    }
    state.telemetry.rain = { state: 1, source: 'master' }; state.revision++
    await act(async () => { await vi.advanceTimersByTimeAsync(100) })
    navigate('雨滴监测'); fireEvent.click(screen.getByRole('button', { name: '暂停动画' }))
    state.telemetry.rain = { state: 0, source: 'master' }; state.revision++
    await act(async () => { await vi.advanceTimersByTimeAsync(100) })
    expect(screen.getByRole('status', { name: '雨滴监测状态' })).toHaveTextContent('无雨')
    navigate('风机控制'); navigate('雨滴监测')
    expect(screen.getByRole('button', { name: '继续动画' })).toBeVisible()
    state.connected = false; state.revision++ // Even an inconsistent stale snapshot must not show a sun.
    await act(async () => { await vi.advanceTimersByTimeAsync(100) })
    expect(screen.getByRole('status', { name: '雨滴监测状态' })).toHaveTextContent('状态未知')
    navigate('粮仓总览')
    expect(screen.getByRole('status', { name: '总览雨滴状态' })).toHaveTextContent('状态未知')
    for (const method of [api.connect, api.disconnect, api.read_once, api.set_fan, api.set_window]) expect(method).not.toHaveBeenCalled()
  })

  it('changes only the visible page while preserving drafts, model state and the bridge', async () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue({} as CanvasRenderingContext2D)
    vi.stubGlobal('requestAnimationFrame', vi.fn().mockReturnValue(1))
    vi.stubGlobal('cancelAnimationFrame', vi.fn())
    const state = initialSnapshot()
    state.port = 'COM32'; state.connected = true; state.revision = 1
    const api = fakeAPI(state)
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    const model = screen.getByRole('img', { name: /粮仓三维示意模型/ })
    fireEvent.click(screen.getByRole('button', { name: '暂停模拟' }))
    fireEvent.change(screen.getByRole('combobox', { name: '串口端口' }), { target: { value: 'COM42' } })
    navigate('风机控制')
    const draft = screen.getByRole('textbox', { name: '风机 1 占空比' })
    fireEvent.change(draft, { target: { value: '63' } })
    for (const title of ['环境监测', '雨滴监测', '声学监测', '窗户控制', '通信日志', '粮仓总览']) {
      navigate(title)
      expect(screen.getByRole('button', { name: `切换到${title}` })).toHaveAttribute('aria-current', 'page')
      expect(document.getElementById('page-title')).toHaveTextContent(title)
      expect(document.getElementById('page-title')).toHaveFocus()
      expect(document.querySelectorAll('.function-page:not([hidden])')).toHaveLength(1)
      expect(screen.getByRole('combobox', { name: '串口端口' })).toHaveValue('COM42')
      expect(screen.getByRole('button', { name: '断开连接' })).toBeVisible()
    }
    expect(screen.getByRole('img', { name: /粮仓三维示意模型/ })).toBe(model)
    expect(screen.getByRole('button', { name: '继续模拟' })).toBeVisible()
    navigate('风机控制')
    expect(screen.getByRole('textbox', { name: '风机 1 占空比' })).toBe(draft)
    expect(draft).toHaveValue('63')
    expect(api.refresh_ports).toHaveBeenCalledTimes(1)
    for (const method of [api.connect, api.disconnect, api.read_once, api.set_fan, api.set_window]) expect(method).not.toHaveBeenCalled()
  })

  it('collects telemetry and logs on hidden pages and follows logs when returning', async () => {
    vi.useFakeTimers()
    const state = initialSnapshot(); state.revision = 1
    const api = fakeAPI(state)
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    navigate('风机控制')
    fireEvent.change(screen.getByRole('textbox', { name: '风机 2 占空比' }), { target: { value: '71' } })
    navigate('粮仓总览')
    state.telemetry.values.master_temp = '26.4 ℃'
    state.telemetry.sounds.sound_rms_1 = '0'
    state.logs = [{ id: 1, text: '[RX] 原始帧完整保留' }]; state.last_log_id = 1
    state.controls.fan_enabled = false; state.revision++
    await act(async () => { await vi.advanceTimersByTimeAsync(100) })
    navigate('环境监测'); expect(screen.getByText('26.4')).toBeVisible()
    navigate('声学监测'); expect(within(document.querySelector('[data-page="acoustic"]') as HTMLElement).getByText('0')).toBeVisible()
    navigate('风机控制')
    expect(screen.getByRole('textbox', { name: '风机 2 占空比' })).toHaveValue('71')
    expect(screen.getByRole('button', { name: '发送风机 2 占空比' })).toBeDisabled()
    const logBody = document.querySelector('.logs-body') as HTMLDivElement
    Object.defineProperty(logBody, 'scrollHeight', { value: 400 })
    navigate('通信日志')
    expect(screen.getByText('[RX] 原始帧完整保留')).toBeVisible()
    expect(logBody.scrollTop).toBe(400)
    expect(api.get_snapshot).toHaveBeenLastCalledWith(0)
    expect(api.refresh_ports).toHaveBeenCalledTimes(1)
  })

  it('cancels an unfinished slider gesture when leaving the page without sending on return', async () => {
    const api = fakeAPI()
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    navigate('风机控制')
    const slider = screen.getByRole('slider', { name: '风机 1 占空比滑块' })
    fireEvent.pointerDown(slider, { button: 0 })
    fireEvent.change(slider, { target: { value: '45' } })
    navigate('粮仓总览'); navigate('风机控制')
    fireEvent.pointerUp(slider)
    expect(api.set_fan).not.toHaveBeenCalled()
    expect(screen.getByRole('textbox', { name: '风机 1 占空比' })).toHaveValue('45')
  })

  it('keeps model temperature simulation separate from telemetry and device commands', async () => {
    const state = initialSnapshot()
    state.telemetry.values.master_temp = '25.2 ℃'
    state.revision = 1
    const api = fakeAPI(state)
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    fireEvent.click(screen.getByRole('button', { name: '暂停模拟' }))
    fireEvent.click(screen.getByRole('button', { name: '放大粮仓模型' }))
    fireEvent.click(screen.getByRole('button', { name: '视角复位' }))
    navigate('环境监测')
    expect(screen.getByText('25.2')).toBeVisible()
    navigate('粮仓总览')
    expect(screen.getByText('模拟温度')).toBeVisible()
    for (const method of [api.connect, api.disconnect, api.read_once, api.set_fan, api.set_window]) expect(method).not.toHaveBeenCalled()
  })

  it('shows master rain three states independently of slave link and clears the status', async () => {
    vi.useFakeTimers()
    const state = initialSnapshot(); state.revision = 1
    state.connected = true
    const api = fakeAPI(state)
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    navigate('环境监测')
    const panel = screen.getByRole('region', { name: '主机雨滴' })
    const status = within(panel).getByRole('status', { name: '主机雨滴状态' })
    expect(status).toHaveTextContent('--')
    state.telemetry.slave_link = '从机链路：离线'
    for (const [value, text] of [[0, '无雨'], [1, '有雨'], [null, '--']] as const) {
      state.telemetry.rain = { state: value, source: 'master' }; state.revision++
      await act(async () => { await vi.advanceTimersByTimeAsync(100) })
      expect(status).toHaveTextContent(text)
    }
    state.telemetry.rain = { state: 1, source: 'master' }; state.revision++
    await act(async () => { await vi.advanceTimersByTimeAsync(100) })
    navigate('风机控制'); navigate('环境监测')
    expect(screen.getByRole('region', { name: '主机雨滴' })).toBe(panel)
    expect(status).toHaveTextContent('有雨')
    state.telemetry = initialSnapshot().telemetry; state.connected = false; state.revision++
    await act(async () => { await vi.advanceTimersByTimeAsync(100) })
    expect(status).toHaveTextContent('--')
    for (const method of [api.connect, api.disconnect, api.read_once, api.set_fan, api.set_window]) expect(method).not.toHaveBeenCalled()
  })

  it('does not display historical rain without master provenance or invent demo rain', async () => {
    const state = initialSnapshot()
    state.telemetry.rain = { state: 1, source: null }; state.revision = 1
    render(<App api={fakeAPI(state)} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    navigate('环境监测')
    expect(screen.getByRole('status', { name: '主机雨滴状态' })).toHaveTextContent('--')
    const demo = new BrowserDemo()
    await demo.connect('DEMO · 模拟控制室')
    await demo.read_once()
    expect((await demo.get_snapshot()).telemetry.rain).toEqual({ state: null, source: null })
    await demo.disconnect()
    expect((await demo.get_snapshot()).telemetry.rain).toEqual({ state: null, source: null })
  })

  it('keeps disconnected fan/read warnings available but disables window actions', async () => {
    const api = fakeAPI()
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    expect(screen.getByRole('button', { name: '读取单帧' })).toBeEnabled()
    navigate('风机控制')
    expect(screen.getByRole('button', { name: '发送风机 1 占空比' })).toBeEnabled()
    navigate('窗户控制')
    expect(screen.getByRole('button', { name: '打开窗户 1' })).toBeDisabled()
    expect(screen.getByRole('button', { name: '关闭窗户 1' })).toBeDisabled()
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
    navigate('风机控制')
    fireEvent.change(screen.getByRole('textbox', { name: '风机 2 占空比' }), { target: { value: '32' } })
    fireEvent.click(screen.getByRole('button', { name: '发送风机 2 占空比' }))
    expect(api.set_fan).toHaveBeenCalledExactlyOnceWith(2, 32)
    navigate('窗户控制')
    fireEvent.click(screen.getByRole('button', { name: '打开窗户 1' }))
    fireEvent.click(screen.getByRole('button', { name: '关闭窗户 1' }))
    expect(api.set_window).toHaveBeenNthCalledWith(1, 1, 1)
    expect(api.set_window).toHaveBeenNthCalledWith(2, 0, 1)
    expect(screen.queryByText(/已确认/)).not.toBeInTheDocument()
  })

  it('routes all four servo buttons to their own channel', async () => {
    const state = initialSnapshot()
    state.connected = true
    state.revision = 1
    state.controls.window_enabled = true
    const api = fakeAPI(state)
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    navigate('窗户控制')
    for (const servo of state.windows) {
      expect(screen.getByRole('button', { name: `打开窗户 ${servo.channel}` })).toBeEnabled()
      fireEvent.click(screen.getByRole('button', { name: `打开窗户 ${servo.channel}` }))
      expect(api.set_window).toHaveBeenLastCalledWith(1, servo.channel)
      fireEvent.click(screen.getByRole('button', { name: `关闭窗户 ${servo.channel}` }))
      expect(api.set_window).toHaveBeenLastCalledWith(0, servo.channel)
    }
  })

  it('renders and submits all four fan channels with independent drafts', async () => {
    const state = initialSnapshot()
    state.connected = true; state.revision = 1
    const api = fakeAPI(state)
    render(<App api={api} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    navigate('风机控制')
    expect(screen.getAllByRole('slider')).toHaveLength(4)
    expect(state.fans.map(fan => fan.pin)).toEqual(['PB1', 'PB8', 'PA1', 'PB9'])
    for (const fan of state.fans) {
      expect(within(screen.getByRole('region', { name: `风机 ${fan.channel}` })).getByText(fan.pin)).toBeVisible()
      fireEvent.change(screen.getByRole('textbox', { name: `风机 ${fan.channel} 占空比` }), { target: { value: String(fan.channel * 25) } })
      fireEvent.click(screen.getByRole('button', { name: `发送风机 ${fan.channel} 占空比` }))
      expect(api.set_fan).toHaveBeenLastCalledWith(fan.channel, fan.channel * 25)
    }
    navigate('环境监测'); navigate('风机控制')
    for (const fan of state.fans) {
      expect(screen.getByRole('textbox', { name: `风机 ${fan.channel} 占空比` })).toHaveValue(String(fan.channel * 25))
    }
    expect(api.set_fan).toHaveBeenCalledTimes(4)
  })

  it('disables fan submissions and reads while a window action is busy', async () => {
    const state = initialSnapshot()
    state.window.busy = true
    state.revision = 1
    state.controls = { read_enabled: false, fan_enabled: false, window_enabled: false }
    render(<App api={fakeAPI(state)} />)
    await act(async () => { await Promise.resolve(); await Promise.resolve() })
    expect(screen.getByRole('button', { name: '读取单帧' })).toBeDisabled()
    navigate('风机控制')
    expect(screen.getByRole('button', { name: '发送风机 1 占空比' })).toBeDisabled()
    expect(screen.getByRole('button', { name: '发送风机 2 占空比' })).toBeDisabled()
    expect(screen.getByRole('button', { name: '发送风机 3 占空比' })).toBeDisabled()
    expect(screen.getByRole('button', { name: '发送风机 4 占空比' })).toBeDisabled()
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
