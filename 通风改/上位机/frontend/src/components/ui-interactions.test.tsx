import { act, cleanup, fireEvent, render, screen, within } from '@testing-library/react'
import { afterEach, expect, it, vi } from 'vitest'
import { WarningMonitor } from './warning-monitor'
import { FanControl } from './fan-control'
import { UltrasonicMonitor } from './ultrasonic-monitor'
import { initialSnapshot, type DesktopAPI, type TrendReport, type WarningEvent } from '../lib/types'
import { Metric } from '../App'

afterEach(() => { cleanup(); vi.restoreAllMocks(); vi.unstubAllGlobals(); vi.useRealTimers() })
const analysis: WarningEvent['analysis'] = { status: 'complete', provider: 'deepseek', model: 'test', mode: 'simulation', result: { summary: '分析摘要', possible_causes: ['原因'], suggested_checks: ['建议'], limitations: '数据限制' } }
const report: TrendReport = { id: 7, checked_at: '12:00', status: 'normal', message: '未发现超限', channels: {}, event_ids: [], stale: false, data_source: 'telemetry', analysis }
const event: WarningEvent = { id: 3, key: 'master_temp', kind: 'temp', source: 'master', status: 'active', occurred_at: '12:00', ended_at: null, read: false, evidence: { current: 25, rate: .6, threshold: .4 }, analysis }
const execute = async (command: () => Promise<{ accepted: boolean }>) => { await command() }

it('folds both analysis sections independently, preserves content and defaults expanded', () => {
  const snapshot = initialSnapshot(); snapshot.warnings.manual = structuredClone(report); snapshot.warnings.events = [structuredClone(event)]
  render(<WarningMonitor snapshot={snapshot} api={null} execute={execute} />)
  const buttons = screen.getAllByRole('button', { name: '收起分析' })
  expect(buttons).toHaveLength(2)
  expect(buttons[0]).toHaveAttribute('aria-expanded', 'true')
  fireEvent.click(buttons[0]); expect(screen.getAllByText('分析摘要')).toHaveLength(2)
  expect(document.querySelectorAll('.analysis-fold[aria-hidden=true]')).toHaveLength(1)
  expect(buttons[1]).toHaveAttribute('aria-expanded', 'true')
  fireEvent.click(screen.getByRole('button', { name: '展开分析' }))
  expect(document.querySelectorAll('.analysis-fold.expanded')).toHaveLength(2)
})

it('confirms event and report removal, cancellation preserves data, and archives by report ID', async () => {
  const snapshot = initialSnapshot(); snapshot.warnings.manual = structuredClone(report); snapshot.warnings.events = [structuredClone(event)]
  const removeEvent = vi.fn().mockResolvedValue({ accepted: true }), removeReport = vi.fn().mockResolvedValue({ accepted: true }), archive = vi.fn().mockResolvedValue({ accepted: true })
  const api = { delete_warning: removeEvent, delete_manual_trend: removeReport, archive_manual_trend: archive } as unknown as DesktopAPI
  render(<WarningMonitor snapshot={snapshot} api={api} execute={execute} />)
  const events = screen.getByRole('region', { name: '预警事件' }), manual = screen.getByRole('region', { name: '主动趋势监测' })
  fireEvent.click(within(events).getByRole('button', { name: '删除' }))
  expect(removeEvent).not.toHaveBeenCalled()
  fireEvent.click(screen.getByRole('button', { name: '取消' })); expect(removeEvent).not.toHaveBeenCalled()
  fireEvent.click(within(events).getByRole('button', { name: '删除' }))
  fireEvent.click(within(screen.getByRole('alertdialog')).getByRole('button', { name: '删除' }))
  expect(removeEvent).toHaveBeenCalledExactlyOnceWith(3)
  fireEvent.click(within(manual).getByRole('button', { name: '删除' }))
  fireEvent.keyDown(screen.getByRole('alertdialog'), { key: 'Escape' }); expect(removeReport).not.toHaveBeenCalled()
  fireEvent.click(within(manual).getByRole('button', { name: '删除' }))
  fireEvent.click(within(screen.getByRole('alertdialog')).getByRole('button', { name: '删除' }))
  expect(removeReport).toHaveBeenCalledExactlyOnceWith(7)
  await act(async () => { fireEvent.click(screen.getByRole('button', { name: '记录到档案' })); await Promise.resolve() })
  expect(archive).toHaveBeenCalledExactlyOnceWith(7)
})

it('synchronizes four shortcuts and slider and animates confirmed PWM only', () => {
  const submit = vi.fn(), fan = { channel: 1, pin: 'PB1', duty: 0, confirmed_duty: null, status: '未发送' }
  const view = render(<FanControl fan={fan} enabled submit={submit} />)
  for (const [label, duty] of [['高', 100], ['中', 90], ['低', 80], ['关闭', 0]] as const) {
    const button = screen.getByRole('button', { name: `${label}${duty}%` })
    fireEvent.click(button); expect(screen.getByRole('slider')).toHaveValue(String(duty)); expect(button).toHaveAttribute('aria-pressed', 'true')
    expect(submit).toHaveBeenLastCalledWith(1, duty)
  }
  expect(submit).toHaveBeenCalledTimes(4)
  view.rerender(<FanControl fan={{ ...fan, duty: 100, confirmed_duty: 100, status: '已确认 100%' }} enabled submit={submit} />)
  expect(document.querySelector('.fan-card')).toHaveClass('speed-high', 'fan-running')
  view.rerender(<FanControl fan={{ ...fan, duty: 90, confirmed_duty: 90, status: '已确认 90%' }} enabled active={false} submit={submit} />)
  expect(document.querySelector('.fan-card')).toHaveClass('speed-medium')
  expect(document.querySelector('.fan-card')).not.toHaveClass('fan-running')
  view.rerender(<FanControl fan={{ ...fan, duty: 80, confirmed_duty: null, status: '确认超时' }} enabled submit={submit} />)
  expect(document.querySelector('.fan-card')).toHaveClass('speed-off')
})

it('renders pressure in kPa and marks only valid near distances red, uses a stable relative height', () => {
  render(<Metric value="101325 Pa" label="气压" unit="kPa" />)
  expect(screen.getByText('101.3')).toBeVisible(); expect(screen.getByText('kPa')).toBeVisible(); cleanup()
  vi.stubGlobal('matchMedia', vi.fn().mockReturnValue({ matches: true, addEventListener: vi.fn(), removeEventListener: vi.fn() }))
  const reading = { valid: true, distance_mm: 500, raw_mm: 500, pulse_us: 2916, age_ms: 100 }
  const view = render(<UltrasonicMonitor reading={reading} connected />)
  const surface = () => Number(document.querySelector('.grain-surface')!.getAttribute('data-surface-y'))
  const low = surface()
  view.rerender(<UltrasonicMonitor reading={{ ...reading, distance_mm: 50, raw_mm: 50, pulse_us: 292 }} connected />)
  expect(surface()).toBeLessThan(low); expect(document.querySelector('.ultrasonic-scene')).toHaveClass('distance-near')
  const high = surface()
  view.rerender(<UltrasonicMonitor reading={{ ...reading, distance_mm: 51 }} connected />)
  expect(surface()).toBe(high); expect(document.querySelector('.ultrasonic-scene')).toHaveClass('distance-near')
  view.rerender(<UltrasonicMonitor reading={{ ...reading, distance_mm: 100 }} connected />)
  expect(document.querySelector('.ultrasonic-scene')).not.toHaveClass('distance-near')
  view.rerender(<UltrasonicMonitor reading={{ ...reading, distance_mm: 50, age_ms: 2000 }} connected />)
  expect(document.querySelector('.ultrasonic-scene')).not.toHaveClass('distance-near')
  expect(screen.getByLabelText('测距状态')).toHaveTextContent('数据不可用')
})

it('eases grain height without delaying the near warning and stops drawing when hidden', () => {
  vi.useFakeTimers()
  const reading = { valid: true, distance_mm: 500, raw_mm: 500, pulse_us: 2916, age_ms: 100 }
  const view = render(<UltrasonicMonitor reading={reading} connected />)
  act(() => { vi.advanceTimersByTime(1000) })
  const surface = () => Number(document.querySelector('.grain-surface')!.getAttribute('data-surface-y'))
  const before = surface()
  view.rerender(<UltrasonicMonitor reading={{ ...reading, distance_mm: 50 }} connected />)
  expect(surface()).toBe(before)
  expect(document.querySelector('.ultrasonic-scene')).toHaveClass('distance-near')
  act(() => { vi.advanceTimersByTime(160) })
  expect(surface()).toBeLessThan(before); expect(surface()).toBeGreaterThan(150)
  view.rerender(<UltrasonicMonitor reading={{ ...reading, distance_mm: 50 }} connected active={false} />)
  const stopped = surface()
  act(() => { vi.advanceTimersByTime(1000) }); expect(surface()).toBe(stopped)
})
