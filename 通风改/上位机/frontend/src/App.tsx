import { useEffect, useRef, useState } from 'react'
import { Activity, ArrowDownToLine, Box, CircleHelp, CloudRain, DoorClosed, DoorOpen, Link2, Radio, RefreshCw, Terminal, Thermometer, Waves, Wind, X } from 'lucide-react'
import { Button } from './components/button'
import { FanControl } from './components/fan-control'
import { PortCombobox } from './components/port-combobox'
import { GranaryView } from './components/granary-view'
import { RainIndicator, RainMonitor, weatherLabels, weatherState } from './components/rain-weather'
import { SmokeMonitor } from './components/smoke-monitor'
import { smokeReading } from './lib/smoke-index'
import { useMonitor } from './lib/use-monitor'
import type { DesktopAPI, Field, Snapshot } from './lib/types'

const metricDefinitions = [
  { suffix: 'temp', label: '温度', unit: '°C' },
  { suffix: 'humidity', label: '湿度', unit: '%' },
  { suffix: 'pressure', label: '绝对气压', unit: 'Pa' },
] as const

const pages = [
  { id: 'overview', title: '粮仓总览', eyebrow: 'OVERVIEW', icon: Box, description: '粮仓三维示意与模拟温度场；设备状态来自当前串口。' },
  { id: 'environment', title: '环境监测', eyebrow: 'ENVIRONMENT', icon: Thermometer, description: '主从 BME280 与主机雨滴实测状态，无有效数据时显示 --。' },
  { id: 'rain', title: '雨滴监测', eyebrow: 'RAINDROP MONITOR', icon: CloudRain, description: '主机雨滴实测状态，以天气动画直观显示有雨或无雨。' },
  { id: 'smoke', title: '烟雾监测', eyebrow: 'SMOKE MONITOR', icon: Activity, description: '从机 MQ-2 相对烟雾指数与独立动画，基于 PA7 现场电压参考，浓度未标定。' },
  { id: 'acoustic', title: '声学监测', eyebrow: 'ACOUSTICS', icon: Waves, description: '查看两路声音数值与当前采集状态。' },
  { id: 'fans', title: '风机控制', eyebrow: 'VENTILATION', icon: Wind, description: '四路独立控制，滑块释放或点击发送后提交占空比。' },
  { id: 'windows', title: '窗户控制', eyebrow: 'WINDOW ACTUATORS', icon: DoorOpen, description: '四路窗户独立操作，确认状态对应各自的舵机编号。' },
  { id: 'logs', title: '通信日志', eyebrow: 'COMMUNICATION', icon: Terminal, description: '完整帧与处理结果，切页期间持续收集日志。' },
] as const
type PageID = typeof pages[number]['id']

export function Metric({ value, label, unit }: { value: string; label: string; unit: string }) {
  const match = value.match(/^(.*?)\s*(℃|°C|%RH|%|Pa)$/u)
  const reading = match ? match[1].trim() : value
  return <div className="metric">
    <span className="metric-label">{label}</span>
    <div className={`metric-value ${reading.startsWith('--') ? 'empty-value' : ''}`}><span>{reading}</span><span className="metric-unit">{match?.[2] ?? unit}</span></div>
  </div>
}

function SensorPanel({ side, snapshot }: { side: 'master' | 'slave'; snapshot: Snapshot }) {
  return <section className="panel sensor-panel" aria-labelledby={`${side}-title`}>
    <div className="panel-heading">
      <div className="heading-name"><span className={`sensor-mark ${side}`} /><h2 id={`${side}-title`}>{side === 'master' ? '主机环境' : '从机环境'}</h2></div>
      <span className="device-label">BME280<span className="device-divider" />{side === 'master' ? 'MASTER' : 'SLAVE'}</span>
    </div>
    <div className="metrics-grid">
      {metricDefinitions.map(metric => <Metric key={metric.suffix} value={snapshot.telemetry.values[`${side}_${metric.suffix}` as Field]} label={metric.label} unit={metric.unit} />)}
    </div>
  </section>
}

export function App({ api: suppliedAPI }: { api?: DesktopAPI }) {
  const { api, snapshot, logs, error, execute } = useMonitor(suppliedAPI)
  const [port, setPort] = useState('')
  const portEdited = useRef(false)
  const preferFirstPort = useRef(false)
  const [dismissedNotice, setDismissedNotice] = useState(0)
  const [page, setPage] = useState<PageID>('overview')
  const currentPage = pages.find(item => item.id === page)!
  const pageTitleRef = useRef<HTMLHeadingElement>(null)
  const logRef = useRef<HTMLDivElement>(null)
  useEffect(() => {
    if (!portEdited.current) setPort(preferFirstPort.current ? snapshot.ports[0] ?? '' : snapshot.port)
  }, [snapshot.port, snapshot.ports])
  useEffect(() => { if (page === 'logs' && logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight }, [logs, page])
  useEffect(() => {
    pageTitleRef.current?.focus({ preventScroll: true })
    document.documentElement.scrollTop = 0
    document.body.scrollTop = 0
  }, [page])
  const notice = snapshot.notice && snapshot.notice.id !== dismissedNotice ? snapshot.notice : null
  const slaveLink = snapshot.telemetry.slave_link.replace(/^从机链路[:：]\s*/, '')
  const weather = weatherState(snapshot.telemetry.rain, snapshot.connected)
  const rainText = weather === 'unknown' ? '--' : weatherLabels[weather]
  const mq2 = snapshot.telemetry.mq2
  const mq2Valid = smokeReading(mq2, snapshot.connected) !== null
  const run = (command: (bridge: DesktopAPI) => Promise<{ accepted: boolean }>) => { if (api) void execute(() => command(api)) }
  return <div className="app-shell paged-shell">
    <aside className="sidebar" aria-label="功能导航">
      <div className="sidebar-brand"><span className="brand-symbol"><Box size={22} /></span><div><strong>智能通风系统</strong><span>GRAIN / MONITOR</span></div></div>
      <div className="nav-group-label">监测与控制</div>
      <nav aria-label="功能页面">{pages.map(item => <button key={item.id} type="button" aria-label={`切换到${item.title}`} aria-current={page === item.id ? 'page' : undefined} className={`nav-item ${page === item.id ? 'selected' : ''}`} onClick={() => setPage(item.id)}><item.icon size={18} strokeWidth={1.6} /><span>{item.title}</span></button>)}</nav>
      <div className="sidebar-footer"><span className={`connection-status ${snapshot.connected ? 'online' : ''}`}><span className="status-dot" />{snapshot.connected ? snapshot.port : '设备未连接'}</span><span>LoRa v4 · 本地运行</span></div>
    </aside>
    <div className="workspace">
    <div className="global-toolbar">
    <header className="app-header">
      <div className="brand"><span className="brand-symbol"><Wind size={26} strokeWidth={1.6} /></span><div><div className="brand-eyebrow">VENTILATION / CONTROL</div><h1>通风监控<span>设备控制台</span></h1></div></div>
      <div className="header-meta">
        {snapshot.demo && <span className="demo-badge">演示模式 · 仅模拟串口</span>}
        <span className={`connection-status ${snapshot.connected ? 'online' : ''}`}><span className="status-dot" />{snapshot.connected ? '串口已连接' : '串口未连接'}</span>
      </div>
    </header>

    <section className="connection-bar panel" aria-label="串口连接">
      <div className="connection-label"><Radio size={18} strokeWidth={1.6} /><span>控制室串口</span></div>
      <PortCombobox value={port} ports={snapshot.ports} ready={!!api} onChange={value => { portEdited.current = true; preferFirstPort.current = false; setPort(value) }} />
      <Button variant="ghost" size="icon" aria-label="刷新串口" title="刷新串口" disabled={!api} onClick={() => { if (!port.trim()) { portEdited.current = false; preferFirstPort.current = true } run(bridge => bridge.refresh_ports()) }}><RefreshCw size={17} /></Button>
      <Button disabled={!api} variant={snapshot.connected ? 'outline' : 'default'} onClick={() => run(bridge => snapshot.connected ? bridge.disconnect() : bridge.connect(port))}><Link2 size={16} />{snapshot.connected ? '断开连接' : '连接设备'}</Button>
      <span className="toolbar-separator" />
      <Button variant="outline" disabled={!api || !snapshot.controls.read_enabled} onClick={() => run(bridge => bridge.read_once())}><ArrowDownToLine size={16} />读取单帧</Button>
      <span className="baud-label">115200<span>baud</span></span>
    </section>
    </div>

    {snapshot.demo && <p className="demo-explanation">仅模拟串口，不识别真实设备。连接设备请打开智能通风系统_正式版.exe。</p>}

    {(notice || error) && <div className="notice" role="alert"><CircleHelp size={17} /><div>{notice && <><strong>{notice.title}</strong><span>{notice.message}</span></>}{error && <span>{error}</span>}</div>{notice && <Button variant="ghost" size="icon" aria-label="关闭提示" onClick={() => setDismissedNotice(notice.id)}><X size={16} /></Button>}</div>}

    <main className="page-content">
      <div className="page-heading"><div><span className="page-eyebrow">{currentPage.eyebrow}</span><h2 ref={pageTitleRef} tabIndex={-1} id="page-title">{currentPage.title}</h2><p>{currentPage.description}</p></div><div className="page-heading-extra">{page === 'overview' && <RainIndicator state={weather} />}<span className="page-index">{String(pages.findIndex(item => item.id === page) + 1).padStart(2, '0')} / {String(pages.length).padStart(2, '0')}</span></div></div>
      <div className="function-page" hidden={page !== 'overview'} data-page="overview" aria-label="粮仓总览内容">
        <div className="overview-status">
          <div className="panel summary-card"><span>控制室连接</span><strong>{snapshot.connected ? snapshot.port : '未连接'}</strong><small>{snapshot.connected ? '串口采集已连接' : '选择端口后连接设备'}</small></div>
          <div className="panel summary-card"><span>从机链路</span><strong>{slaveLink}</strong><small>来自遥测帧的链路状态</small></div>
          <div className="panel summary-card"><span>最近有效遥测</span><strong>{snapshot.telemetry.updated_at ?? '--'}</strong><small>模型模拟不改变实测数据</small></div>
        </div>
        <GranaryView active={page === 'overview'} />
      </div>
      <div className="function-page" hidden={page !== 'environment'} data-page="environment" aria-label="环境监测内容">
        <div className="column-heading"><span>双 BME280 · 实测</span><span className="section-subtitle">{snapshot.telemetry.updated_at ? `更新于 ${snapshot.telemetry.updated_at}` : '等待有效遥测'}</span></div>
        <div className="environment-panels">
        <SensorPanel side="master" snapshot={snapshot} />
        <SensorPanel side="slave" snapshot={snapshot} />
        </div>
        <section className="panel rain-panel" aria-labelledby="rain-title">
          <div className="panel-heading"><div className="heading-name"><h2 id="rain-title">主机雨滴</h2></div><span className="device-label">PA11<span className="device-divider" />DO</span></div>
          <div className={`rain-value metric-value ${rainText === '--' ? 'empty-value' : ''}`} role="status" aria-label="主机雨滴状态">{rainText}</div>
          <p className="acoustic-help">未知显示 --；仅检测上传，不自动停风机或关窗。</p>
        </section>
      </div>
      <div className="function-page" hidden={page !== 'rain'} data-page="rain" aria-label="雨滴监测内容">
        <RainMonitor state={weather} updatedAt={snapshot.telemetry.updated_at} active={page === 'rain'} />
      </div>
      <div className="function-page" hidden={page !== 'smoke'} data-page="smoke" aria-label="烟雾监测内容">
        <SmokeMonitor mq2={mq2} connected={snapshot.connected} active={page === 'smoke'} />
        <section className="panel sensor-panel" aria-labelledby="smoke-title">
          <div className="panel-heading"><div className="heading-name"><Activity size={18} /><h2 id="smoke-title">从机烟雾传感器</h2></div><span className="device-label">MQ-2<span className="device-divider" />PA7 · AO</span></div>
          <div className="metrics-grid smoke-metrics">
            <Metric label="ADC 原始值" value={mq2Valid ? String(mq2.raw) : '--'} unit="计数" />
            <Metric label="PA7 输入电压" value={mq2Valid ? (mq2.pa7_mv! / 1000).toFixed(3) : '--'} unit="V" />
            <Metric label="AO 还原电压" value={mq2Valid ? (mq2.ao_mv! / 1000).toFixed(3) : '--'} unit="V" />
            <Metric label="已知采样年龄" value={mq2Valid ? String(mq2.age_ms) : '--'} unit="ms" />
          </div>
          <div className="link-row"><span>采样状态 · 未标定</span><span className={`link-state ${mq2Valid ? 'online' : ''}`} role="status" aria-label="烟雾采样状态">{mq2Valid ? '有效模拟量' : '数据不可用'}</span></div>
          <p className="acoustic-help">电压按从机配置的分压比例还原，尚未标定烟雾浓度。采样有效不代表模块接线正常或预热完成。</p>
        </section>
      </div>
      <div className="function-page" hidden={page !== 'acoustic'} data-page="acoustic" aria-label="声学监测内容">
        <section className="panel acoustic-panel" aria-labelledby="acoustic-title">
          <div className="panel-heading"><div className="heading-name"><Waves size={18} strokeWidth={1.6} /><h2 id="acoustic-title">从机声学</h2></div><span className="device-label">RMS<span className="device-divider" />PCM</span></div>
          <div className="sounds-grid">
            <div className="sound-metric"><span className="metric-label">声音 1 · 左声道</span><div className="sound-value">{snapshot.telemetry.sounds.sound_rms_1}<span>计数</span></div><span className="micro-label">ADC 采集待接入</span></div>
            <div className="sound-metric"><span className="metric-label">声音 2 · 右声道</span><div className="sound-value">{snapshot.telemetry.sounds.sound_rms_2}<span>计数</span></div><span className="micro-label">ADC 采集待接入</span></div>
          </div>
          <p className="acoustic-help">旧 SPH0645 采集已停用，MAX4466 的 ADC 采集待接入；当前无有效声音数据显示 --。声音数值不是分贝。</p>
          <div className="link-row"><span><Activity size={14} />从机链路</span><span className={`link-state ${slaveLink === '在线' ? 'online' : ''}`}><span className="status-dot" />{slaveLink}</span></div>
        </section>
      </div>

      <div className="function-page" hidden={page !== 'fans'} data-page="fans" aria-label="风机控制内容">
        <section className="panel fans-panel" aria-label="风机控制">
          {snapshot.fans.map(fan => <FanControl key={fan.channel} fan={fan} active={page === 'fans'} enabled={!!api && snapshot.controls.fan_enabled} submit={(channel, duty) => run(bridge => bridge.set_fan(channel, duty))} />)}
          <p className="control-help">滑块释放后发送，或输入 0–100 后点击发送。占空比为 PWM 高电平比例，ACK 不代表实测转速。</p>
        </section>
      </div>
      <div className="function-page" hidden={page !== 'windows'} data-page="windows" aria-label="窗户控制内容">
        <section className="panel window-panel" aria-labelledby="window-title">
          <div className="panel-heading"><div className="heading-name"><DoorOpen size={18} strokeWidth={1.6} /><h2 id="window-title">窗户控制</h2></div><span className="device-label">从机 · 四路独立</span></div>
          <div className="servo-grid">{snapshot.windows.map(servo => <div key={servo.channel} className="servo-control">
            <div className="panel-heading"><span>舵机 {servo.channel}</span><span className="device-label">{servo.pin}</span></div>
            <div className="window-actions"><Button variant="outline" aria-label={`打开窗户 ${servo.channel}`} disabled={!api || !snapshot.controls.window_enabled} onClick={() => run(bridge => bridge.set_window(1, servo.channel))}><DoorOpen size={16} />打开窗户</Button><Button variant="outline" aria-label={`关闭窗户 ${servo.channel}`} disabled={!api || !snapshot.controls.window_enabled} onClick={() => run(bridge => bridge.set_window(0, servo.channel))}><DoorClosed size={16} />关闭窗户</Button></div>
            <div className="window-state" role="status"><span className="status-dot muted" /><span>{servo.status}</span></div>
          </div>)}</div>
          <div className="window-help"><p>360° 连续旋转：开窗 1700 μs，关窗 1300 μs，各运行 300 ms 后设置 1500 μs 停止脉宽；每只舵机的停止脉宽需要分别空载校准。</p><p><strong>ACK 只确认启动 PWM 已设置，不表示动作完成、自动停止成功或机械到位。</strong>等待确认时暂停新增遥测与风机指令；超时结果未知，不自动重试。</p></div>
        </section>
      </div>
      <div className="function-page" hidden={page !== 'logs'} data-page="logs" aria-label="通信日志内容">
    <section className="panel logs-panel" aria-labelledby="logs-title">
      <div className="logs-heading"><div className="heading-name"><Terminal size={16} strokeWidth={1.7} /><h2 id="logs-title">通信日志</h2><span className="log-count">{logs.length} 条</span></div><span className="log-follow"><span className="status-dot" />自动跟随</span></div>
      <div className="logs-body" ref={logRef} tabIndex={0} aria-label="帧日志" aria-live="off">{logs.length ? logs.map(log => <div className="log-line" key={log.id}>{log.text}</div>) : <div className="logs-empty">等待串口通信，完整帧与处理结果将在此显示。</div>}</div>
    </section>
      </div>
    </main>
    <footer className="app-footer"><span>LoRa 应用协议 v4</span><span>本地设备控制 · 离线运行</span></footer>
    </div>
  </div>
}
