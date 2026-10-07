import { act, cleanup, fireEvent, render, screen, within } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { WarningMonitor } from './warning-monitor'
import { BrowserDemo } from '../lib/demo'
import { initialSnapshot, type DesktopAPI } from '../lib/types'

afterEach(() => { cleanup(); vi.useRealTimers(); vi.restoreAllMocks() })
const execute = async (command: () => Promise<{ accepted: boolean }>) => { await command() }

describe('local warnings and independent provider drafts', () => {
  it('provides the manual button in real mode with automatic warnings off', async () => {
    const state = initialSnapshot()
    const monitor = vi.fn().mockResolvedValue({accepted:true})
    render(<WarningMonitor snapshot={state} api={{monitor_trends:monitor} as unknown as DesktopAPI} execute={execute} />)
    expect(screen.getByRole('button',{name:'手动趋势监测'})).toBeEnabled()
    expect(screen.getByLabelText('启用自动温湿度趋势预警')).not.toBeChecked()
    fireEvent.click(screen.getByRole('button',{name:'手动趋势监测'}))
    expect(monitor).toHaveBeenCalledTimes(1)
  })

  it('checks a stable trend with no event and still shows its explanation', async () => {
    vi.useFakeTimers()
    const demo = new BrowserDemo()
    await demo.update_warning_settings(false,0.4,1)
    await demo.set_warning_scenario('normal')
    await demo.monitor_trends(); await demo.monitor_trends()
    expect(vi.getTimerCount()).toBe(1)
    let state = await demo.get_snapshot()
    expect(state.warnings.manual?.status).toBe('normal')
    expect(state.warnings.manual?.channels.master_temp.span_seconds).toBe(120)
    expect(state.warnings.events).toHaveLength(0)
    const view = render(<WarningMonitor snapshot={state} api={demo} execute={execute} />)
    expect(screen.getByRole('button',{name:'正在分析…'})).toBeDisabled()
    expect(screen.getByText('温湿度检查：本次未发现配置中的上升趋势超限。')).toBeInTheDocument()
    expect(screen.getAllByText(/120\/120 秒/)).toHaveLength(4)
    await act(async()=>{vi.advanceTimersByTime(750)})
    state = await demo.get_snapshot(); view.rerender(<WarningMonitor snapshot={state} api={demo} execute={execute} />)
    expect(screen.getByText('模拟解释，未调用 API')).toBeInTheDocument()
    expect(screen.getByRole('button',{name:'手动趋势监测'})).toBeEnabled()
    await demo.disconnect()
  })

  it('manually creates one warning while automatic mode is off and never calls a network', async () => {
    vi.useFakeTimers()
    const network = vi.spyOn(globalThis,'fetch').mockRejectedValue(new Error('forbidden'))
    const demo = new BrowserDemo()
    await demo.update_warning_settings(false,0.4,1)
    await demo.set_warning_scenario('warming')
    expect((await demo.get_snapshot()).warnings.events).toHaveLength(0)
    await demo.monitor_trends(); vi.advanceTimersByTime(750)
    await demo.monitor_trends(); vi.advanceTimersByTime(750)
    const state = await demo.get_snapshot()
    expect(state.warnings.events).toHaveLength(1)
    expect(state.warnings.events[0].trigger).toBe('manual')
    render(<WarningMonitor snapshot={state} api={demo} execute={execute} />)
    expect(screen.getByText('手动检查发现 · 未使用自动预警的30秒确认条件')).toBeInTheDocument()
    expect(screen.getByText('上升趋势超限')).toBeInTheDocument()
    expect(network).not.toHaveBeenCalled()
    await demo.disconnect()
  })

  it('shows missing data and an API error without pretending a complete normal assessment', async () => {
    const demo = new BrowserDemo()
    await demo.monitor_trends()
    const state = await demo.get_snapshot()
    render(<WarningMonitor snapshot={state} api={demo} execute={execute} />)
    expect(state.warnings.manual?.status).toBe('unavailable')
    expect(screen.getByRole('alert')).toHaveTextContent('没有可用数据')
    expect(screen.queryByText('本次未发现配置中的上升趋势超限。')).not.toBeInTheDocument()
    await demo.disconnect()
  })

  it('discards a pending manual explanation after switching provider', async () => {
    vi.useFakeTimers()
    const demo = new BrowserDemo()
    await demo.set_warning_scenario('normal'); await demo.monitor_trends()
    await demo.update_ai_settings('kimi'); vi.advanceTimersByTime(750)
    const state = await demo.get_snapshot()
    expect(state.warnings.manual?.analysis.status).toBe('stale')
    render(<WarningMonitor snapshot={state} api={demo} execute={execute} />)
    expect(screen.getByText(/结果已过期，请重新检查/)).toBeInTheDocument()
    expect(screen.queryByText('模拟解释，未调用 API')).not.toBeInTheDocument()
    await demo.disconnect()
  })

  it('switches vendors, retains drafts, disables secrets and only queues configuration', () => {
    const state = initialSnapshot()
    state.ai_settings.mode = 'simulation'
    const update = vi.fn().mockResolvedValue({ accepted: true })
    const api = { update_ai_settings: update } as unknown as DesktopAPI
    render(<WarningMonitor snapshot={state} api={api} execute={execute} />)
    const settings = screen.getByText('API 设置 · 本地模拟').parentElement as HTMLDetailsElement
    fireEvent.click(screen.getByText('API 设置 · 本地模拟'))
    expect(screen.getByLabelText('API Key')).toBeDisabled()
    expect(screen.getByLabelText('API Key')).toHaveAttribute('type', 'password')
    expect(screen.getByRole('button', { name: '测试 API' })).toBeDisabled()
    fireEvent.change(screen.getByLabelText('模型 ID'), { target: { value: 'deepseek-draft' } })
    fireEvent.change(screen.getByLabelText('API 厂家'), { target: { value: 'kimi' } })
    expect(screen.getByLabelText('模型 ID')).toHaveValue('kimi-k3')
    fireEvent.change(screen.getByLabelText('模型 ID'), { target: { value: 'kimi-draft' } })
    fireEvent.change(screen.getByLabelText('API 厂家'), { target: { value: 'deepseek' } })
    expect(screen.getByLabelText('模型 ID')).toHaveValue('deepseek-draft')
    fireEvent.change(screen.getByLabelText('API 厂家'), { target: { value: 'kimi' } })
    expect(screen.getByLabelText('模型 ID')).toHaveValue('kimi-draft')
    fireEvent.change(screen.getByLabelText('API 厂家'), { target: { value: 'custom' } })
    expect(screen.getByLabelText('模型 ID')).toHaveValue('')
    fireEvent.change(screen.getByLabelText('接口地址'), { target: { value: 'http://bad' } })
    fireEvent.click(screen.getByRole('button', { name: '应用厂家配置' }))
    expect(within(settings.parentElement!).getByRole('alert')).toHaveTextContent('HTTPS')
    expect(update).not.toHaveBeenCalledWith('custom', 'http://bad', '')
    expect(update).toHaveBeenCalledWith('kimi')
  })

  it('shows evidence, generates explicit offline explanation, and read does not restore event', async () => {
    const demo = new BrowserDemo()
    await demo.set_warning_scenario('warming')
    let state = await demo.get_snapshot()
    const view = render(<WarningMonitor snapshot={state} api={demo} execute={execute} />)
    const event = state.warnings.events[0]
    expect(screen.getByText('主机 · 温度上升')).toBeInTheDocument()
    expect(screen.queryByText('模拟解释，未调用 API')).not.toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: '标记已读' }))
    state = await demo.get_snapshot()
    expect(state.warnings.events[0].status).toBe('active')
    expect(state.warnings.events[0].read).toBe(true)
    vi.useFakeTimers()
    await act(async () => { fireEvent.click(screen.getByRole('button', { name: '分析原因' })) })
    state = await demo.get_snapshot(); view.rerender(<WarningMonitor snapshot={state} api={demo} execute={execute} />)
    expect(screen.getByRole('button', { name: '分析中…' })).toBeDisabled()
    await act(async () => { vi.advanceTimersByTime(750) })
    state = await demo.get_snapshot(); view.rerender(<WarningMonitor snapshot={state} api={demo} execute={execute} />)
    expect(screen.getByText('模拟解释，未调用 API')).toBeInTheDocument()
    expect(state.warnings.events[0].id).toBe(event.id)
    await demo.disconnect()
  })

  it('drops provider-stale tasks and merges duplicate clicks without network', async () => {
    vi.useFakeTimers()
    const fetch = vi.spyOn(globalThis, 'fetch').mockRejectedValue(new Error('no network'))
    const demo = new BrowserDemo()
    await demo.set_warning_scenario('warming')
    const id = (await demo.get_snapshot()).warnings.events[0].id
    await demo.analyze_warning(id); await demo.analyze_warning(id)
    expect(vi.getTimerCount()).toBe(1)
    await demo.update_ai_settings('kimi', 'https://api.moonshot.cn/v1', 'kimi-k3')
    vi.advanceTimersByTime(750)
    expect((await demo.get_snapshot()).warnings.events[0].analysis.status).toBe('stale')
    expect(fetch).not.toHaveBeenCalled()
    await demo.disconnect()
  })

  it('validates rule input and hides demo scenarios in real mode', () => {
    const state = initialSnapshot()
    const apply = vi.fn().mockResolvedValue({ accepted: true })
    render(<WarningMonitor snapshot={state} api={{ update_warning_settings: apply } as unknown as DesktopAPI} execute={execute} />)
    expect(screen.queryByText('模拟验收')).not.toBeInTheDocument()
    fireEvent.change(screen.getByLabelText('温度变化速度（℃/分钟）'), { target: { value: 'NaN' } })
    fireEvent.click(screen.getByRole('button', { name: '应用' }))
    expect(screen.getByRole('alert')).toHaveTextContent('有限正数')
    expect(apply).not.toHaveBeenCalled()
  })

  it('loads backend profiles after the first poll without replacing unsaved drafts', () => {
    const state = initialSnapshot()
    const view = render(<WarningMonitor snapshot={state} api={null} execute={execute} />)
    const next = structuredClone(state); next.revision = 1; next.ai_settings.provider = 'kimi'
    next.ai_settings.profiles.kimi.model = 'saved-kimi-model'
    view.rerender(<WarningMonitor snapshot={next} api={null} execute={execute} />)
    expect(screen.getByLabelText('模型 ID')).toHaveValue('saved-kimi-model')
    fireEvent.change(screen.getByLabelText('模型 ID'), { target: { value: 'unsaved-draft' } })
    const later = structuredClone(next); later.revision++
    view.rerender(<WarningMonitor snapshot={later} api={null} execute={execute} />)
    expect(screen.getByLabelText('模型 ID')).toHaveValue('unsaved-draft')
  })

  it('keeps an active warning visible after analysis failure and permits retry', async () => {
    const demo = new BrowserDemo()
    await demo.set_warning_scenario('warming')
    const state = await demo.get_snapshot()
    state.warnings.events[0].analysis = { status: 'error', result: { error: '模拟分析失败，请重试' }, provider: 'deepseek', model: 'deepseek-flash' }
    const analyze = vi.fn().mockResolvedValue({ accepted: true })
    render(<WarningMonitor snapshot={state} api={{ analyze_warning: analyze } as unknown as DesktopAPI} execute={execute} />)
    expect(screen.getByRole('alert')).toHaveTextContent('模拟分析失败')
    expect(screen.getByText('活动 · 未读')).toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: '分析原因' }))
    expect(analyze).toHaveBeenCalledWith(state.warnings.events[0].id)
    await demo.disconnect()
  })

  it('submits a per-vendor secret once and clears the password input', async () => {
    const state = initialSnapshot(); state.revision = 1
    const save = vi.fn().mockResolvedValue({ accepted: true })
    const api = { save_ai_config: save, update_ai_settings: vi.fn().mockResolvedValue({accepted:true}) } as unknown as DesktopAPI
    render(<WarningMonitor snapshot={state} api={api} execute={execute} />)
    fireEvent.click(screen.getByText('API 设置 · 真实 API'))
    expect(screen.getByLabelText('API Key')).toBeEnabled()
    fireEvent.change(screen.getByLabelText('API Key'), {target:{value:'qa-only-deepseek-secret'}})
    fireEvent.change(screen.getByLabelText('API 厂家'), {target:{value:'kimi'}})
    expect(screen.getByLabelText('API Key')).toHaveValue('')
    fireEvent.change(screen.getByLabelText('API Key'), {target:{value:'qa-only-kimi-secret'}})
    await act(async () => { fireEvent.click(screen.getByRole('button',{name:'应用厂家配置'})) })
    expect(save).toHaveBeenCalledWith('kimi','https://api.moonshot.cn/v1','kimi-k3','api','qa-only-kimi-secret',true)
    expect(screen.getByLabelText('API Key')).toHaveValue('')
    fireEvent.change(screen.getByLabelText('API 厂家'), {target:{value:'deepseek'}})
    expect(screen.getByLabelText('API Key')).toHaveValue('qa-only-deepseek-secret')
  })

  it('shows real analysis without claiming it is simulated and enables a configured API test', async () => {
    const demo = new BrowserDemo(); await demo.set_warning_scenario('warming')
    const state = await demo.get_snapshot(); state.ai_settings.mode = 'api'
    state.ai_settings.profiles.deepseek.has_key = true; state.ai_settings.profiles.deepseek.remembered = true
    state.ai_settings.connection = {status:'complete',message:'API连接成功，模型返回格式有效'}
    state.warnings.events[0].analysis = {status:'complete',provider:'deepseek',model:'deepseek-flash',mode:'api',data_source:'demo',result:{summary:'实际模型回复',possible_causes:[],suggested_checks:[],limitations:'模拟数据'}}
    const test = vi.fn().mockResolvedValue({accepted:true})
    render(<WarningMonitor snapshot={state} api={{test_ai_connection:test} as unknown as DesktopAPI} execute={execute} />)
    expect(screen.getByText('真实 API 分析 · 输入为模拟数据')).toBeInTheDocument()
    expect(screen.queryByText('模拟解释，未调用 API')).not.toBeInTheDocument()
    fireEvent.click(screen.getByText('API 设置 · 真实 API'))
    expect(screen.getByLabelText('API Key')).toHaveValue('')
    expect(screen.getByRole('button',{name:'测试 API'})).toBeEnabled()
    fireEvent.click(screen.getByRole('button',{name:'测试 API'}))
    expect(test).toHaveBeenCalledTimes(1)
    await demo.disconnect()
  })
})
