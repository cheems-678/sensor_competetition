import { cleanup, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it } from 'vitest'
import { EnvironmentTrend } from './environment-trend'
import { appendObservation, emptyHistory } from '../lib/environment-history'
import { initialSnapshot } from '../lib/types'

afterEach(cleanup)

describe('environment trend display', () => {
  it('defaults to temperature and switches both series and units, with keyboard inspection', () => {
    const snapshot = initialSnapshot(); snapshot.connected = true; snapshot.port = 'COM32'
    snapshot.telemetry.updated_at = '12:00:00'
    snapshot.telemetry.values = { master_temp: '0 °C', slave_temp: '24 °C', master_humidity: '50 %RH', slave_humidity: '60 %', master_pressure: '101325 Pa', slave_pressure: '100982 Pa' }
    let history = emptyHistory()
    for (let id = 1; id <= 3; id++) { snapshot.telemetry.sample_id = id; history = appendObservation(history, snapshot, 1000 * id) }
    render(<EnvironmentTrend points={history.points} connected />)
    expect(screen.getByRole('button', { name: '温度' })).toHaveAttribute('aria-pressed', 'true')
    const chart = screen.getByRole('img', { name: '主从机温度变化曲线' })
    expect(document.querySelectorAll('.trend-series path')).toHaveLength(2)
    expect(screen.getByLabelText('曲线读数')).toHaveTextContent('主机0°C')
    fireEvent.keyDown(chart, { key: 'Home' })
    expect(document.querySelector('.trend-reading time')).toHaveTextContent(new Date(1000).toLocaleTimeString('zh-CN', { hour12: false }))
    fireEvent.click(screen.getByRole('button', { name: '湿度' }))
    expect(screen.getByRole('img', { name: '主从机湿度变化曲线' })).toBeVisible()
    expect(screen.getByLabelText('曲线读数')).toHaveTextContent('主机50%')
    fireEvent.click(screen.getByRole('button', { name: '气压' }))
    expect(screen.getByRole('img', { name: '主从机气压变化曲线' })).toBeVisible()
    expect(screen.getByLabelText('曲线读数')).toHaveTextContent('101.3kPa')
    expect(screen.getByText(/记录本次连接/)).not.toBeVisible()
    fireEvent.click(screen.getByText('说明'))
    expect(screen.getByText(/记录本次连接/)).toBeVisible()
  })
  it('shows an empty state without fabricated lines and retains the selected metric when data updates', () => {
    const { rerender } = render(<EnvironmentTrend points={[]} connected={false} />)
    expect(screen.getByRole('status')).toHaveTextContent('连接后显示曲线')
    expect(document.querySelector('.trend-series')).not.toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: '气压' }))
    rerender(<EnvironmentTrend points={[]} connected />)
    expect(screen.getByRole('button', { name: '气压' })).toHaveAttribute('aria-pressed', 'true')
    expect(screen.getByRole('status')).toHaveTextContent('等待有效遥测')
  })
})
