import { useEffect, useRef, useState } from 'react'
import { Activity, ArrowDownToLine, CircleHelp, DoorClosed, DoorOpen, Link2, Radio, RefreshCw, Terminal, Waves, Wind, X } from 'lucide-react'
import { Button } from './components/button'
import { FanControl } from './components/fan-control'
import { PortCombobox } from './components/port-combobox'
import { useMonitor } from './lib/use-monitor'
import type { DesktopAPI, Field, Snapshot } from './lib/types'

const metricDefinitions = [
  { suffix: 'temp', label: '温度', unit: '°C' },
  { suffix: 'humidity', label: '湿度', unit: '%' },
  { suffix: 'pressure', label: '绝对气压', unit: 'Pa' },
] as const

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
  const logRef = useRef<HTMLDivElement>(null)
  useEffect(() => {
    if (!portEdited.current) setPort(preferFirstPort.current ? snapshot.ports[0] ?? '' : snapshot.port)
  }, [snapshot.port, snapshot.ports])
  useEffect(() => { if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight }, [logs])
  const notice = snapshot.notice && snapshot.notice.id !== dismissedNotice ? snapshot.notice : null
  const slaveLink = snapshot.telemetry.slave_link.replace(/^从机链路[:：]\s*/, '')
  const run = (command: (bridge: DesktopAPI) => Promise<{ accepted: boolean }>) => { if (api) void execute(() => command(api)) }
  return <div className="app-shell">
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

    {snapshot.demo && <p className="demo-explanation">仅模拟串口，不识别真实设备。连接设备请打开智能通风系统_正式版.exe。</p>}

    {(notice || error) && <div className="notice" role="alert"><CircleHelp size={17} /><div>{notice && <><strong>{notice.title}</strong><span>{notice.message}</span></>}{error && <span>{error}</span>}</div>{notice && <Button variant="ghost" size="icon" aria-label="关闭提示" onClick={() => setDismissedNotice(notice.id)}><X size={16} /></Button>}</div>}

    <main className="dashboard">
      <div className="data-column">
        <div className="column-heading"><span>环境与声学</span><span className="section-subtitle">{snapshot.telemetry.updated_at ? `更新于 ${snapshot.telemetry.updated_at}` : '等待有效遥测'}</span></div>
        <SensorPanel side="master" snapshot={snapshot} />
        <SensorPanel side="slave" snapshot={snapshot} />
        <section className="panel acoustic-panel" aria-labelledby="acoustic-title">
          <div className="panel-heading"><div className="heading-name"><Waves size={18} strokeWidth={1.6} /><h2 id="acoustic-title">从机声学</h2></div><span className="device-label">RMS<span className="device-divider" />PCM</span></div>
          <div className="sounds-grid">
            <div className="sound-metric"><span className="metric-label">声音 1 · 左声道</span><div className="sound-value">{snapshot.telemetry.sounds.sound_rms_1}<span>计数</span></div><span className="micro-label">SEL 接 GND</span></div>
            <div className="sound-metric"><span className="metric-label">声音 2 · 右声道</span><div className="sound-value">{snapshot.telemetry.sounds.sound_rms_2}<span>计数</span></div><span className="micro-label">SEL 接 3V3</span></div>
          </div>
          <p className="acoustic-help">最新约 63.7 ms 完整采样窗的去直流 RMS；界面约每秒查询一次。单位：18 位 PCM 计数，不是分贝；无有效数据显示 --。</p>
          <div className="link-row"><span><Activity size={14} />从机链路</span><span className={`link-state ${slaveLink === '在线' ? 'online' : ''}`}><span className="status-dot" />{slaveLink}</span></div>
        </section>
      </div>

      <div className="control-column">
        <div className="column-heading"><span>设备控制</span><span className="section-subtitle">手动控制</span></div>
        <section className="panel fans-panel" aria-label="风机控制">
          {snapshot.fans.map(fan => <FanControl key={fan.channel} fan={fan} enabled={!!api && snapshot.controls.fan_enabled} submit={(channel, duty) => run(bridge => bridge.set_fan(channel, duty))} />)}
          <p className="control-help">滑块释放后发送，或输入 0–100 后点击发送。占空比为 PWM 高电平比例，ACK 不代表实测转速。</p>
        </section>
        <section className="panel window-panel" aria-labelledby="window-title">
          <div className="panel-heading"><div className="heading-name"><DoorOpen size={18} strokeWidth={1.6} /><h2 id="window-title">窗户控制</h2></div><span className="device-label">从机 PB8</span></div>
          <div className="window-actions"><Button variant="outline" disabled={!api || !snapshot.controls.window_enabled} onClick={() => run(bridge => bridge.set_window(1))}><DoorOpen size={16} />打开窗户</Button><Button variant="outline" disabled={!api || !snapshot.controls.window_enabled} onClick={() => run(bridge => bridge.set_window(0))}><DoorClosed size={16} />关闭窗户</Button></div>
          <div className="window-state" role="status"><span className="status-dot muted" /><span>{snapshot.window.status}</span></div>
          <div className="window-help"><p>360° 连续旋转：开窗 1700 μs，关窗 1300 μs，各运行 300 ms 后设置 1500 μs 停止脉宽；1500 μs 需要空载校准。</p><p><strong>ACK 只确认启动 PWM 已设置，不表示动作完成、自动停止成功或机械到位。</strong>等待确认时暂停新增遥测与风机指令；超时结果未知，不自动重试。</p></div>
        </section>
      </div>
    </main>

    <section className="panel logs-panel" aria-labelledby="logs-title">
      <div className="logs-heading"><div className="heading-name"><Terminal size={16} strokeWidth={1.7} /><h2 id="logs-title">通信日志</h2><span className="log-count">{logs.length} 条</span></div><span className="log-follow"><span className="status-dot" />自动跟随</span></div>
      <div className="logs-body" ref={logRef} tabIndex={0} aria-label="帧日志" aria-live="off">{logs.length ? logs.map(log => <div className="log-line" key={log.id}>{log.text}</div>) : <div className="logs-empty">等待串口通信，完整帧与处理结果将在此显示。</div>}</div>
    </section>
    <footer className="app-footer"><span>LoRa 应用协议 v4</span><span>本地设备控制 · 离线运行</span></footer>
  </div>
}
