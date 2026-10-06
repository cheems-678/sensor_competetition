import { initialSnapshot, type Accepted, type DesktopAPI, type Snapshot } from './types'
import { decimalDraft } from './duty'

// Browser-only preview. No serial API, database or remote endpoint is used here.
export class BrowserDemo implements DesktopAPI {
  private state = initialSnapshot()
  private logId = 0
  private noticeId = 0
  private nextReading = 0
  private sampleId = 0
  private mq2ReceivedAt = Date.now()
  private mq2SourceAge = 0
  private previewSmokeMv: number | null = null
  private timers = new Set<ReturnType<typeof setTimeout>>()
  constructor() {
    this.state.demo = true
    this.state.ports = ['DEMO · 模拟控制室']
    this.state.port = this.state.ports[0]
    this.log('演示模式：不连接硬件，不写入数据库。')
    const query = new URLSearchParams(window.location.search)
    if (query.get('fixture') === 'smoke') {
      const mv = Number(query.get('mv') ?? 350)
      this.previewSmokeMv = Number.isInteger(mv) && mv >= 0 && mv <= 3300 ? mv : 350
      this.state.connected = true
      this.reading()
      this.sync()
    }
    if (query.get('fixture') === 'limits') {
      this.state.connected = true
      this.state.window.status = '开窗启动PWM已确认（完成/停止状态未知）；ACK 不表示动作完成、自动停止成功或机械窗户到位。'
      this.state.telemetry = {
        values: { master_temp: '-3276.7 °C', slave_temp: '3276.7 °C', master_humidity: '6553.4 %', slave_humidity: '6553.4 %', master_pressure: '4294967294 Pa', slave_pressure: '4294967294 Pa' },
        sounds: { sound_rms_1: '0', sound_rms_2: '4294967294' }, slave_link: '未知（旧布局）', updated_at: '2026-10-04 23:59:59', sample_id: ++this.sampleId,
        rain: { state: null, source: null },
        mq2: { valid: true, raw: 4095, pa7_mv: 3300, ao_mv: 6600, age_ms: 0 },
        ultrasonic: { valid: true, distance_mm: 500, raw_mm: 500, pulse_us: 2915, age_ms: 0 },
      }
      this.nextReading = Infinity
      this.sync()
    }
  }
  private log(message: string) {
    const stamp = new Date().toLocaleTimeString('zh-CN', { hour12: false })
    this.state.logs.push({ id: ++this.logId, text: `[${stamp}] ${message}` })
    this.state.last_log_id = this.logId
    this.state.revision++
  }
  private notice(title: string, message: string) { this.state.notice = { id: ++this.noticeId, title, message } }
  private sync() {
    const busy = this.state.window.busy
    this.state.controls = { read_enabled: !busy, fan_enabled: !busy, window_enabled: this.state.connected && !busy }
  }
  private later(callback: () => void) {
    const timer = setTimeout(() => { this.timers.delete(timer); callback() }, 700)
    this.timers.add(timer)
  }
  private reading() {
    const smokeMv = this.previewSmokeMv ?? 1000
    this.mq2ReceivedAt = Date.now(); this.mq2SourceAge = 60
    this.state.telemetry = {
      values: { master_temp: '24.6 °C', slave_temp: '23.8 °C', master_humidity: '48.2 %', slave_humidity: '51.4 %', master_pressure: '101326 Pa', slave_pressure: '101284 Pa' },
      sounds: { sound_rms_1: '0', sound_rms_2: '186' }, slave_link: '在线', updated_at: new Date().toLocaleTimeString('zh-CN', { hour12: false }), sample_id: ++this.sampleId,
      rain: { state: null, source: null },
      mq2: { valid: true, raw: Math.round(smokeMv / 3300 * 4095), pa7_mv: smokeMv, ao_mv: smokeMv * 2, age_ms: 60 },
      ultrasonic: { valid: true, distance_mm: 250, raw_mm: 252, pulse_us: 1469, age_ms: 60 },
    }
    this.nextReading = Date.now() + 1000
    this.state.revision++
  }
  async refresh_ports(): Promise<Accepted> { this.state.revision++; return { accepted: true } }
  async connect(port: string): Promise<Accepted> {
    if (!port.trim()) { this.notice('端口为空', '请输入或选择控制室串口'); return { accepted: true } }
    this.state.connected = true
    this.state.port = port
    this.state.window.status = '已连接，尚未发送，位置未知'
    this.state.windows = this.state.windows.map(servo => ({ ...servo, status: this.state.window.status }))
    this.nextReading = Date.now() + 1000
    this.sync()
    this.log(`演示连接 ${port}`)
    return { accepted: true }
  }
  async disconnect(): Promise<Accepted> {
    for (const timer of this.timers) clearTimeout(timer)
    this.timers.clear()
    this.state.connected = false
    this.state.window = { busy: false, status: '已断开，位置未知' }
    this.state.windows = this.state.windows.map(servo => ({ ...servo, status: this.state.window.status }))
    this.state.fans = this.state.fans.map(fan => ({ ...fan, status: '已断开，状态未知' }))
    this.state.telemetry = initialSnapshot().telemetry
    this.sync()
    this.log('演示连接已断开')
    return { accepted: true }
  }
  async read_once(): Promise<Accepted> {
    if (!this.state.connected) this.notice('未连接', '请先连接控制室串口')
    else if (!this.state.window.busy) { this.reading(); this.log('演示遥测更新') }
    return { accepted: true }
  }
  async set_fan(channel: number, duty: number | string): Promise<Accepted> {
    if (!this.state.connected) { this.notice('未连接', '请先连接控制室串口'); return { accepted: true } }
    if (this.state.window.busy) return { accepted: true }
    const number = typeof duty === 'string' ? decimalDraft(duty) : duty
    if (!Number.isFinite(number) || number < 0 || number > 100) { this.notice('输入错误', '请输入 0–100 的占空比'); return { accepted: true } }
    const fan = this.state.fans.find(item => item.channel === channel)
    if (!fan) return { accepted: false }
    fan.duty = Math.floor(number + 0.5)
    fan.status = `等待确认 ${fan.duty}%`
    this.log(`演示风机 ${channel} 提交 ${fan.duty}%`)
    this.later(() => { fan.status = `已确认 ${fan.duty}%`; this.state.revision++ })
    return { accepted: true }
  }
  async set_window(action: number, servo_id = 1): Promise<Accepted> {
    if (!this.state.controls.window_enabled || !Number.isInteger(servo_id) || servo_id < 1 || servo_id > 4 || (action !== 0 && action !== 1)) return { accepted: false }
    const label = action === 1 ? '开窗' : '关窗'
    this.state.window = { busy: true, status: `${label}等待确认` }
    this.state.windows[servo_id - 1].status = this.state.window.status
    this.sync()
    this.log(`演示${label}提交`)
    this.later(() => {
      this.state.window = { busy: false, status: `${label}启动PWM已确认（完成/停止状态未知）` }
      this.state.windows[servo_id - 1].status = this.state.window.status
      this.sync()
      this.log(`演示${label}启动PWM已确认`)
    })
    return { accepted: true }
  }
  async get_snapshot(after_log_id = 0): Promise<Snapshot> {
    if (this.state.connected && !this.state.window.busy && Date.now() >= this.nextReading && !this.state.fans.some(fan => fan.status.startsWith('等待'))) this.reading()
    if (this.state.telemetry.mq2.valid) {
      const age = this.mq2SourceAge + Date.now() - this.mq2ReceivedAt
      this.state.telemetry.mq2 = age >= 2000 ? initialSnapshot().telemetry.mq2 : { ...this.state.telemetry.mq2, age_ms: age }
      this.state.revision++
    }
    if (this.state.telemetry.ultrasonic.valid) {
      const age = this.mq2SourceAge + Date.now() - this.mq2ReceivedAt
      this.state.telemetry.ultrasonic = age >= 2000 ? initialSnapshot().telemetry.ultrasonic : { ...this.state.telemetry.ultrasonic, age_ms: age }
    }
    return structuredClone({ ...this.state, logs: this.state.logs.filter(log => log.id > after_log_id) })
  }
}
