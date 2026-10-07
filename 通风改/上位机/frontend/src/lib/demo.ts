import { initialSnapshot, type Accepted, type DesktopAPI, type Snapshot, type Provider, type WarningEvent, type TrendChannel, type TrendReport } from './types'
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
  private warningId = 0
  private manualId = 0
  private analysisGeneration = 0
  async delete_warning(id: number): Promise<Accepted> {
    this.state.warnings.events = this.state.warnings.events.filter(event => event.id !== id)
    this.state.warnings.active_count = this.state.warnings.events.filter(event => ['active', 'unavailable'].includes(event.status)).length
    this.state.revision++
    return { accepted: true }
  }
  async delete_manual_trend(id: number): Promise<Accepted> {
    if (this.state.warnings.manual?.id !== id) throw new Error('本次报告已变化')
    this.state.warnings.manual = null; this.state.revision++
    return { accepted: true }
  }
  async archive_manual_trend(id: number): Promise<Accepted> {
    const report = this.state.warnings.manual
    if (report?.id !== id) throw new Error('本次报告已变化')
    if (report.analysis.status === 'pending') throw new Error('请等待分析完成')
    if (!report.archive_id) {
      report.archive_id = `preview-${id}`
      this.state.warnings.archives = [structuredClone(report), ...(this.state.warnings.archives ?? [])].slice(0, 200)
      this.state.revision++
    }
    return { accepted: true }
  }
  constructor() {
    this.state.demo = true
    this.state.warnings.enabled = true
    this.state.ai_settings.mode = 'simulation'
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
        sounds: { sound_rms_1: '--', sound_rms_2: '--', sound_p2p_1: '0', sound_p2p_2: '4095', sound_p2p_3: '2048', sound_p2p_4: '128', sound_p2p_5: '16' }, slave_link: '未知（旧布局）', updated_at: '2026-10-04 23:59:59', sample_id: ++this.sampleId,
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
      sounds: { sound_rms_1: '--', sound_rms_2: '--', sound_p2p_1: '0', sound_p2p_2: '186', sound_p2p_3: '93', sound_p2p_4: '47', sound_p2p_5: '25' }, slave_link: '在线', updated_at: new Date().toLocaleTimeString('zh-CN', { hour12: false }), sample_id: ++this.sampleId,
      rain: { state: null, source: null },
      mq2: { valid: true, raw: Math.round(smokeMv / 3300 * 4095), pa7_mv: smokeMv, ao_mv: smokeMv * 2, age_ms: 60 },
      ultrasonic: { valid: true, distance_mm: 250, raw_mm: 252, pulse_us: 1469, age_ms: 60 },
    }
    this.nextReading = Date.now() + 1000
    this.state.revision++
  }
  async refresh_ports(): Promise<Accepted> { this.state.revision++; return { accepted: true } }
  async get_ai_settings() { return structuredClone(this.state.ai_settings) }
  async save_ai_config(provider: Provider, base_url: string, model: string, mode: 'api' | 'simulation', api_key = '', _remember = true): Promise<Accepted> {
    await this.update_ai_settings(provider, base_url, model)
    this.state.ai_settings.mode = mode
    const previous = this.state.ai_settings.profiles[provider]
    previous.has_key = Boolean(api_key) || Boolean(previous.has_key)
    previous.remembered = false // Browser preview never persists credentials.
    this.state.revision++
    return { accepted: true }
  }
  async test_ai_connection(): Promise<Accepted> {
    this.state.ai_settings.connection = { status: 'error', message: '浏览器预览不调用真实 API，请使用桌面接口版测试' }
    this.state.revision++
    return { accepted: true }
  }
  async update_ai_settings(provider: Provider, base_url?: string, model?: string): Promise<Accepted> {
    this.state.ai_settings.provider = provider
    if (base_url !== undefined && model !== undefined) this.state.ai_settings.profiles[provider] = { base_url, model }
    this.analysisGeneration++
    this.state.warnings.events.forEach(event => { if (event.analysis.status !== 'idle') event.analysis.status = 'stale' })
    if (this.state.warnings.manual) { this.state.warnings.manual.stale = true; this.state.warnings.manual.analysis.status = 'stale' }
    this.state.revision++
    return { accepted: true }
  }
  async update_warning_settings(enabled: boolean, temp_rate: number, humidity_rate: number): Promise<Accepted> {
    if (![temp_rate, humidity_rate].every(value => Number.isFinite(value) && value > 0)) throw new Error('变化速度必须为有限正数')
    const changed = temp_rate !== this.state.warnings.rates.temp || humidity_rate !== this.state.warnings.rates.humidity
    this.state.warnings.enabled = enabled
    this.state.warnings.rates = { temp: temp_rate, humidity: humidity_rate }
    this.state.warnings.events.forEach(event => {
      if (event.kind !== 'communication' && (changed || (!enabled && event.trigger !== 'manual')) && ['active','unavailable'].includes(event.status)) {
        event.status = 'stopped'; event.ended_at = new Date().toISOString()
      }
    })
    this.state.warnings.active_count = this.state.warnings.events.filter(event => ['active','unavailable'].includes(event.status)).length
    if (changed) { this.analysisGeneration++; if (this.state.warnings.manual) { this.state.warnings.manual.stale = true; this.state.warnings.manual.analysis.status = 'stale' } }
    this.state.revision++
    return { accepted: true }
  }
  private stopWarnings() {
    this.analysisGeneration++
    if (this.state.warnings.manual) { this.state.warnings.manual.stale = true; this.state.warnings.manual.analysis.status = 'stale' }
    this.state.warnings.metrics = {}
    this.state.warnings.active_count = 0
    this.state.warnings.events.forEach(event => {
      if (event.status === 'active' || event.status === 'unavailable') { event.status = 'stopped'; event.ended_at = new Date().toISOString() }
      if (event.analysis.status === 'pending') event.analysis.status = 'stale'
    })
  }
  async mark_warning_read(id: number): Promise<Accepted> {
    const event = this.state.warnings.events.find(event => event.id === id)
    if (event) event.read = true
    this.state.revision++
    return { accepted: true }
  }
  async monitor_trends(): Promise<Accepted> {
    if (this.state.warnings.manual?.analysis.status === 'pending') return {accepted:true}
    const channels: Record<string, TrendChannel> = {}
    const event_ids: number[] = []
    for (const key of ['master_temp','master_humidity','slave_temp','slave_humidity'] as const) {
      const metric = this.state.warnings.metrics[key]
      const current = Number.parseFloat(this.state.telemetry.values[key])
      const available = Boolean(metric || (this.state.connected && Number.isFinite(current)))
      const kind = key.endsWith('temp') ? 'temp' : 'humidity'
      const source = key.startsWith('master') ? 'master' : 'slave'
      const rate = metric?.rate ?? null
      const complete = available && (metric?.span_seconds ?? 0) >= 120 && rate != null
      const threshold = this.state.warnings.rates[kind]
      const exceeded = complete && rate >= threshold
      const deadband = Math.min(kind === 'temp' ? 0.02 : 0.05, threshold * 0.1)
      channels[key] = {source,kind,state:complete?'ready':available?'insufficient':'unavailable', direction:rate==null?'unknown':rate>deadband?'rising':rate < -deadband?'falling':'stable', current:Number.isFinite(current)?current:null,start:null,rate,threshold,span_seconds:metric?.span_seconds ?? 0,sample_count:metric ? 121 : available ? 1 : 0,exceeded,min:null,max:null}
      if (exceeded) {
        let event = this.state.warnings.events.find(item => item.key === key && ['active','unavailable'].includes(item.status))
        const evidence = {current:channels[key].current ?? 0,rate:rate!,threshold,window_seconds:120,sample_count:121}
        if (!event) {
          event = {id:++this.warningId,key,source,kind,trigger:'manual',status:'active',occurred_at:new Date().toISOString(),ended_at:null,read:false,evidence,analysis:{status:'idle',result:null,provider:null,model:null}}
          this.state.warnings.events.unshift(event)
        } else { event.status = 'active'; event.evidence = evidence }
        event_ids.push(event.id)
      }
    }
    const status = event_ids.length ? 'abnormal' : Object.values(channels).every(item=>item.state==='ready') ? 'normal' : Object.values(channels).some(item=>item.state==='ready') ? 'partial' : Object.values(channels).some(item=>item.state==='insufficient') ? 'insufficient' : 'unavailable'
    const messages = {abnormal:'手动检查发现上升趋势超限，已生成或更新预警。',normal:'本次未发现配置中的上升趋势超限。',partial:'部分通道未发现超限，其余数据不足或不可用，不能完整判定。',insufficient:'有效数据不足两分钟，仅显示短时趋势，尚不能完整判定预警。',unavailable:'没有可用温湿度数据，请确认连接及传感器读数。'}
    const provider = this.state.ai_settings.provider
    const report: TrendReport = {id:++this.manualId,checked_at:new Date().toISOString(),status,message:messages[status],channels,event_ids,stale:false,data_source:'demo',analysis:{status:'pending',provider,model:this.state.ai_settings.profiles[provider].model,mode:this.state.ai_settings.mode,data_source:'demo',result:null}}
    this.state.warnings.manual = report
    this.state.warnings.active_count = this.state.warnings.events.filter(item=>['active','unavailable'].includes(item.status)).length
    if (status === 'unavailable' || this.state.ai_settings.mode === 'api') {
      report.analysis.status = 'error'; report.analysis.result = {error: status === 'unavailable' ? '没有可用数据，未请求AI；请先确认实测读数。' : '浏览器预览不调用真实 API，请使用桌面版分析'}
    } else {
      const generation = this.analysisGeneration
      this.later(() => { if (generation !== this.analysisGeneration || this.state.warnings.manual !== report) return
        report.analysis.status = 'complete'; report.analysis.analyzed_at = new Date().toISOString()
        report.analysis.result = {summary:report.message,possible_causes:['需要结合现场条件解释'],suggested_checks:['继续观察温湿度变化'],limitations:'模拟解释，未调用 API；数据不足时不能完整判定。'}
        this.state.revision++
      })
    }
    this.state.revision++
    return {accepted:true}
  }
  async analyze_warning(id: number): Promise<Accepted> {
    const event = this.state.warnings.events.find(event => event.id === id)
    if (!event || event.analysis.status === 'pending') return { accepted: true }
    if (this.state.ai_settings.mode === 'api') {
      event.analysis.status = 'error'; event.analysis.result = { error: '浏览器预览不调用真实 API，请使用桌面接口版分析' }
      this.state.revision++
      return { accepted: true }
    }
    const generation = this.analysisGeneration
    const provider = this.state.ai_settings.provider
    event.analysis = { status: 'pending', result: null, provider, model: this.state.ai_settings.profiles[provider].model, mode: 'simulation', data_source: 'demo' }
    this.state.revision++
    this.later(() => {
      if (generation !== this.analysisGeneration || !this.state.warnings.events.includes(event)) return
      event.analysis.status = 'complete'; event.analysis.analyzed_at = new Date().toISOString()
      event.analysis.result = { summary: event.kind === 'communication' ? '遥测应答超时，环境状态未知。' : `实测趋势超过配置变化速度，建议检查现场条件。`, possible_causes: ['环境条件或测量状态变化'], suggested_checks: ['检查实测读数及设备通信状态'], limitations: '模拟解释，未调用 API；不能据此确定故障原因或粮食状态。' }
      this.state.revision++
    })
    return { accepted: true }
  }
  async set_warning_scenario(name: string): Promise<Accepted> {
    this.stopWarnings()
    const warming = ['warming', 'recovery', 'reconnect'].includes(name)
    const kind = name === 'timeout' ? 'communication' : name === 'humidity' ? 'humidity' : 'temp'
    const rate = kind === 'humidity' ? 2 : 0.6
    const threshold = kind === 'humidity' ? this.state.warnings.rates.humidity : this.state.warnings.rates.temp
    if (name === 'timeout' || (this.state.warnings.enabled && (warming || name === 'humidity') && rate >= threshold)) {
      const event: WarningEvent = { id: ++this.warningId, key: kind === 'communication' ? 'communication' : `master_${kind}`, kind,
        source: kind === 'communication' ? 'link' : 'master', status: name === 'recovery' || name === 'timeout' ? 'resolved' : name === 'reconnect' ? 'stopped' : 'active',
        occurred_at: new Date(Date.now() - 70000).toISOString(), ended_at: null, read: false,
        evidence: kind === 'communication' ? { message: '遥测应答超时，当前环境状态未知' } : { current: kind === 'temp' ? 28 : 63.33, rate, threshold, window_seconds: 120, sample_count: 121 },
        analysis: { status: 'idle', result: null, provider: null, model: null } }
      if (event.status === 'resolved' || event.status === 'stopped') event.ended_at = new Date().toISOString()
      this.state.warnings.events.unshift(event)
      this.state.warnings.events = this.state.warnings.events.slice(0, 200)
      this.state.warnings.active_count = event.status === 'active' ? 1 : 0
    }
    this.state.warnings.metrics = { master_temp: { span_seconds: name === 'reconnect' ? 10 : 120, rate: name === 'reconnect' ? null : warming ? 0.6 : 0 }, master_humidity: { span_seconds: 120, rate: name === 'humidity' ? 2 : 0 },slave_temp:{span_seconds:120,rate:0},slave_humidity:{span_seconds:120,rate:0} }
    this.state.telemetry.values.master_temp = `${warming ? 28 : 24} °C`
    this.state.telemetry.values.master_humidity = `${name === 'humidity' ? 63.33 : 50} %RH`
    this.state.telemetry.values.slave_temp = '24.2 °C'; this.state.telemetry.values.slave_humidity = '51.3 %RH'
    this.state.revision++
    return { accepted: true }
  }
  async connect(port: string): Promise<Accepted> {
    if (!port.trim()) { this.notice('端口为空', '请输入或选择控制室串口'); return { accepted: true } }
    this.stopWarnings()
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
    this.stopWarnings()
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
    const submitted = fan.duty
    this.later(() => { if (!this.state.connected || fan.duty !== submitted) return; fan.status = `已确认 ${submitted}%`; fan.confirmed_duty = submitted; this.state.revision++ })
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
