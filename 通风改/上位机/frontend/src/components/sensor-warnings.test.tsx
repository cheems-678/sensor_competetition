import { cleanup, fireEvent, render, screen, within } from '@testing-library/react'
import { afterEach, expect, it, vi } from 'vitest'
import { SensorChecks } from './sensor-checks'
import { SmokeMonitor } from './smoke-monitor'
import { UltrasonicMonitor } from './ultrasonic-monitor'
import { WarningMonitor } from './warning-monitor'
import { initialSnapshot, type DesktopAPI, type SensorCheck, type SensorReview, type TrendReport } from '../lib/types'

afterEach(cleanup)
const readings: SensorReview['readings'] = {
  rain: { source: 'master', kind: 'rain', state: 'abnormal', current: 1, threshold: 1, unit: '状态', message: '下雨（降雨提示）' },
  smoke: { source: 'slave', kind: 'smoke', state: 'abnormal', current: 11, threshold: 10, unit: '相对指数', message: '烟雾相对指数超限' },
  distance: { source: 'slave', kind: 'distance', state: 'abnormal', current: 10.5, threshold: 10, unit: 'cm', message: '粮面距离过近：等待≥11 cm连续有效3秒恢复' },
}
it('shows rain, smoke and latched distance status, and never labels missing data normal', () => {
  const view = render(<SensorChecks readings={readings} />)
  expect(screen.getByRole('status', { name: '雨滴检测预警状态' })).toHaveTextContent('下雨')
  expect(screen.getByRole('status', { name: '烟雾相对指数预警状态' })).toHaveTextContent('超限')
  expect(screen.getByText('10.5 cm')).toBeVisible()
  view.rerender(<SensorChecks readings={{rain:{...readings.rain!,state:'normal',current:0,message:'无雨'}}} />)
  expect(screen.getByRole('status', { name: '雨滴检测预警状态' })).toHaveTextContent('无雨')
  expect(screen.getByRole('status', { name: '烟雾相对指数预警状态' })).toHaveTextContent('数据不可用')
  expect(screen.getByRole('status', { name: '粮面测距预警状态' })).toHaveTextContent('数据不可用')
})
it('keeps distance red during backend recovery confirmation, and clears red when unavailable', () => {
  const distance = {valid:true,distance_mm:105,raw_mm:105,pulse_us:612,age_ms:0}
  const view = render(<UltrasonicMonitor reading={distance} connected warning={readings.distance} />)
  expect(document.querySelector('.ultrasonic-scene')).toHaveClass('distance-near')
  expect(screen.getByRole('status',{name:'测距状态'})).toHaveTextContent('等待≥11 cm')
  view.rerender(<UltrasonicMonitor reading={{...distance,distance_mm:110}} connected warning={{...readings.distance!,state:'normal',current:11,message:'正常检测'}} />)
  expect(document.querySelector('.ultrasonic-scene')).not.toHaveClass('distance-near')
  view.rerender(<UltrasonicMonitor reading={{...distance,age_ms:2000}} connected warning={readings.distance} />)
  expect(document.querySelector('.ultrasonic-scene')).not.toHaveClass('distance-near')
  expect(screen.getByRole('status',{name:'测距状态'})).toHaveTextContent('数据不可用')
})
it('uses strict 10 cm distance boundary and never alarms an invalid 4.9 cm reading', () => {
  const data = {valid:true,distance_mm:100,raw_mm:100,pulse_us:583,age_ms:0}
  const view = render(<UltrasonicMonitor reading={data} connected />)
  expect(document.querySelector('.ultrasonic-scene')).not.toHaveClass('distance-near')
  view.rerender(<UltrasonicMonitor reading={{...data,distance_mm:99}} connected />)
  expect(document.querySelector('.ultrasonic-scene')).toHaveClass('distance-near')
  view.rerender(<UltrasonicMonitor reading={{...data,distance_mm:49}} connected />)
  expect(document.querySelector('.ultrasonic-scene')).not.toHaveClass('distance-near')
})
it('shows smoke 10 as normal and 11 as abnormal using the existing rounded PA7 index', () => {
  const mq = {valid:true,raw:1000,pa7_mv:559,ao_mv:1118,age_ms:0}
  const view = render(<SmokeMonitor mq2={mq} connected active={false} />)
  expect(screen.getByLabelText('相对烟雾指数')).toHaveTextContent('10/ 100')
  expect(screen.getByRole('status',{name:'烟雾预警状态'})).toHaveTextContent('正常检测')
  view.rerender(<SmokeMonitor mq2={{...mq,pa7_mv:560,ao_mv:1120}} connected active={false} warning={readings.smoke} />)
  expect(document.querySelector('.smoke-monitor')).toHaveClass('smoke-abnormal')
  expect(screen.getByRole('status',{name:'烟雾预警状态'})).toHaveTextContent('超限')
  view.rerender(<SmokeMonitor mq2={{...mq,age_ms:2000}} connected active={false} warning={readings.smoke} />)
  expect(document.querySelector('.smoke-monitor')).not.toHaveClass('smoke-abnormal')
  expect(screen.getByRole('status',{name:'烟雾预警状态'})).toHaveTextContent('数据不可用')
})
it('integrates all three event types with AI, read and confirmed deletion and archived manual evidence', () => {
  const state = initialSnapshot()
  state.warnings.sensors = readings
  state.warnings.events = Object.values(readings).map((sensor: SensorCheck, i) => ({id:i+1,key:sensor.kind,kind:sensor.kind,source:sensor.source,status:'active',occurred_at:'12:00',ended_at:null,read:false,evidence:{current:sensor.current!,threshold:sensor.threshold,unit:sensor.unit,trigger_value:sensor.current!,detected_at:'12:00'},analysis:{status:'idle',result:null,provider:null,model:null}}))
  state.warnings.active_count = 3
  const report: TrendReport = {id:7,checked_at:'12:00',status:'insufficient',message:'温湿度数据不足',channels:{},event_ids:[1,2,3],sensor_review:{status:'abnormal',readings,event_ids:[1,2,3]},stale:false,data_source:'telemetry',analysis:{status:'complete',provider:'deepseek',model:'test',result:{summary:'传感器检查',possible_causes:['现场条件'],suggested_checks:['核对'],limitations:'模拟'}}}
  state.warnings.manual = report
  state.warnings.archives = [{...report,archive_id:'fixture'}]
  const analyze = vi.fn().mockResolvedValue({accepted:true}), read = vi.fn().mockResolvedValue({accepted:true}), remove = vi.fn().mockResolvedValue({accepted:true})
  render(<WarningMonitor snapshot={state} api={{analyze_warning:analyze,mark_warning_read:read,delete_warning:remove} as unknown as DesktopAPI} execute={async action=>{await action()}} />)
  const events = screen.getByRole('region',{name:'预警事件'})
  expect(within(events).getByText('主机 · 降雨提示')).toBeVisible()
  expect(within(events).getByText('从机 · 烟雾相对指数超限')).toBeVisible()
  expect(within(events).getByText('从机 · 粮面距离过近')).toBeVisible()
  fireEvent.click(within(events).getAllByRole('button',{name:'分析原因'})[0]); expect(analyze).toHaveBeenCalledWith(1)
  fireEvent.click(within(events).getAllByRole('button',{name:'标记已读'})[1]); expect(read).toHaveBeenCalledWith(2)
  fireEvent.click(within(events).getAllByRole('button',{name:'删除'})[2]); expect(remove).not.toHaveBeenCalled()
  fireEvent.click(screen.getByRole('button',{name:'取消'})); expect(remove).not.toHaveBeenCalled()
  fireEvent.click(within(events).getAllByRole('button',{name:'删除'})[2])
  fireEvent.click(within(screen.getByRole('alertdialog')).getByRole('button',{name:'删除'})); expect(remove).toHaveBeenCalledWith(3)
  expect(within(screen.getByRole('region',{name:'主动趋势监测'})).getByText('附加检查：存在预警条件')).toBeVisible()
  const archive = screen.getByRole('region',{name:'趋势档案'})
  fireEvent.click(within(archive).getByText(/12:00 · 实测/))
  expect(within(archive).getByText('11 / 100')).toBeVisible()
})
