import { cleanup, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it } from 'vitest'
import { UltrasonicMonitor } from './ultrasonic-monitor'
import { initialSnapshot } from '../lib/types'

afterEach(cleanup)
describe('ultrasonic readings', () => {
  it('shows one decimal cm and diagnostics, clears at expiry and disconnect, and recovers', () => {
    const reading = { valid: true, distance_mm: 250, raw_mm: 252, pulse_us: 1469, age_ms: 1999 }
    const { rerender } = render(<UltrasonicMonitor reading={reading} connected />)
    expect(screen.getByRole('img', { name: '粮仓剖面：仓顶传感器到水平粮面的竖直虚线表示距离' })).toBeVisible()
    expect(screen.getByText('到粮面表面的距离')).toBeVisible()
    expect(screen.getByText('25.0')).toBeVisible(); expect(screen.getByText('25.2')).toBeVisible()
    expect(screen.getByText('1469')).toBeVisible(); expect(screen.getByLabelText('测距状态')).toHaveTextContent('有效测量')
    expect(document.querySelector('details')).not.toHaveAttribute('open')
    rerender(<UltrasonicMonitor reading={{ ...reading, age_ms: 2000 }} connected />)
    expect(document.querySelector('.ultrasonic-scene')).toHaveClass('measurement-unavailable')
    expect(screen.queryByText('25.0')).not.toBeInTheDocument(); expect(screen.getAllByText('--')).toHaveLength(4)
    rerender(<UltrasonicMonitor reading={reading} connected={false} />)
    expect(screen.getByLabelText('测距状态')).toHaveTextContent('数据不可用')
    rerender(<UltrasonicMonitor reading={initialSnapshot().telemetry.ultrasonic} connected />)
    expect(screen.getAllByText('--')).toHaveLength(4)
    rerender(<UltrasonicMonitor reading={{ ...reading, distance_mm: 100, raw_mm: 500 }} connected />)
    expect(screen.getByText('10.0')).toBeVisible(); expect(screen.getByText('50.0')).toBeVisible()
  })
})
