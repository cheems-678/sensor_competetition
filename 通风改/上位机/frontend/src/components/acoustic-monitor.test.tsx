import { cleanup, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { AcousticMonitor } from './acoustic-monitor'
import { initialSnapshot } from '../lib/types'

afterEach(() => { cleanup(); vi.restoreAllMocks(); vi.unstubAllGlobals() })
function sounds(values = ['0', '186', '93', '47', '25']) {
  const result = initialSnapshot().telemetry.sounds
  values.forEach((value, index) => { result[`sound_p2p_${index + 1}` as 'sound_p2p_1'] = value })
  return result
}
const edit = (channel: number, value: string) => fireEvent.change(screen.getByLabelText(`声音 ${channel} 阈值`), { target: { value } })
const apply = () => fireEvent.click(screen.getByRole('button', { name: '应用阈值' }))
const states = (selector: string) => Array.from(document.querySelectorAll(selector), node => node.getAttribute('data-state'))

describe('five-channel model and manual local thresholds', () => {
  it('starts unconfigured and does not change judgment while editing; apply affects all points/cards together', () => {
    render(<AcousticMonitor sounds={sounds()} connected active={false} />)
    expect(states('.acoustic-point')).toEqual(Array(5).fill('unset'))
    edit(1, '0'); edit(2, '185'); edit(3, '93'); edit(4, '46'); edit(5, '0')
    expect(states('.acoustic-point')).toEqual(Array(5).fill('unset'))
    apply()
    const expected = ['normal', 'abnormal', 'normal', 'abnormal', 'abnormal']
    expect(states('.acoustic-point')).toEqual(expected)
    expect(states('.acoustic-channel')).toEqual(expected)
    expect(document.querySelectorAll('.sound-value')[0]).toHaveTextContent('0ADC计数')
    edit(2, '4095')
    expect(screen.getByLabelText('声音 2 状态')).toHaveTextContent('幅度超限')
    apply()
    expect(screen.getByLabelText('声音 2 状态')).toHaveTextContent('未超阈值')
  })
  it('rejects bad input atomically, and blank disables only that channel', () => {
    render(<AcousticMonitor sounds={sounds()} connected active={false} />)
    edit(2, '0'); edit(3, '0'); apply()
    edit(2, '4095'); edit(3, 'wrong'); apply()
    expect(screen.getByRole('alert')).toHaveTextContent('声音 3')
    expect(screen.getByLabelText('声音 2 状态')).toHaveTextContent('幅度超限')
    edit(3, ''); apply()
    expect(screen.getByLabelText('声音 2 状态')).toHaveTextContent('未超阈值')
    expect(screen.getByLabelText('声音 3 状态')).toHaveTextContent('待设置阈值')
  })
  it('immediately clears alarms on missing/old/timeout/disconnect and keeps settings across reconnect/navigation', () => {
    const { rerender, unmount } = render(<AcousticMonitor sounds={sounds()} connected active={false} />)
    edit(2, '100'); apply()
    rerender(<AcousticMonitor sounds={sounds()} connected active />)
    expect(screen.getByLabelText('声音 2 状态')).toHaveTextContent('幅度超限')
    rerender(<AcousticMonitor sounds={sounds(['--', '--', '--', '--', '--'])} connected active={false} />)
    expect(states('.acoustic-channel')).toEqual(Array(5).fill('unavailable'))
    rerender(<AcousticMonitor sounds={sounds()} connected={false} active={false} />)
    expect(screen.getByLabelText('声音 2 阈值')).toHaveValue('100')
    expect(document.querySelectorAll('.sound-value')[1]).toHaveTextContent('--')
    rerender(<AcousticMonitor sounds={sounds(['0', '100', '93', '47', '25'])} connected active={false} />)
    expect(screen.getByLabelText('声音 2 状态')).toHaveTextContent('未超阈值')
    unmount()
    render(<AcousticMonitor sounds={sounds()} connected active={false} />)
    expect(screen.getByLabelText('声音 2 阈值')).toHaveValue('')
    expect(screen.getByLabelText('声音 2 状态')).toHaveTextContent('待设置阈值')
  })
  it('retains readable values/statuses if Canvas is unavailable, and has no simulated temperature', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null)
    render(<AcousticMonitor sounds={sounds()} connected active />)
    expect(screen.getByText(/当前环境无法显示三维模型/)).toBeVisible()
    edit(2, '0'); apply()
    expect(screen.getByLabelText('声音 2 状态')).toHaveTextContent('幅度超限')
    expect(document.querySelectorAll('.sound-value')[1]).toHaveTextContent('186')
    expect(document.querySelector('.thermal-readings')).toBeNull()
  })
})
