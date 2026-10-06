export interface LogEntry { id: number; text: string }
export interface ServoState { channel: number; pin: string; status: string }
export interface FanState { channel: number; pin: string; duty: number; status: string }
export type Field = 'master_temp' | 'slave_temp' | 'master_humidity' | 'slave_humidity' | 'master_pressure' | 'slave_pressure'
export interface UltrasonicReading { valid: boolean; distance_mm: number | null; raw_mm: number | null; pulse_us: number | null; age_ms: number | null }
export type Provider = 'deepseek' | 'kimi' | 'custom'
export interface AIProfile { base_url: string; model: string; has_key?: boolean; remembered?: boolean }
export interface AISettings { provider: Provider; profiles: Record<Provider, AIProfile>; mode: 'api' | 'simulation'
  persistent?: boolean
  connection: { status: 'idle' | 'pending' | 'complete' | 'error' | 'stale'; message: string; provider?: string | null; model?: string | null; tested_at?: string } }
export interface WarningEvent {
  id: number; key: string; source: 'master' | 'slave' | 'link'; kind: 'temp' | 'humidity' | 'communication'
  status: 'active' | 'unavailable' | 'resolved' | 'stopped'; occurred_at: string; ended_at: string | null; read: boolean
  trigger?: 'manual' | 'automatic'
  evidence: { current?: number; rate?: number; threshold?: number; window_seconds?: number; sample_count?: number; message?: string }
  analysis: { status: 'idle' | 'pending' | 'complete' | 'error' | 'stale'; provider: string | null; model: string | null; analyzed_at?: string
    mode?: 'api' | 'simulation'; data_source?: 'demo' | 'telemetry'
    result: { summary?: string; possible_causes?: string[]; suggested_checks?: string[]; limitations?: string; error?: string } | null }
}
export interface TrendChannel {
  source: 'master' | 'slave'; kind: 'temp' | 'humidity'
  state: 'ready' | 'insufficient' | 'unavailable'; direction: 'rising' | 'falling' | 'stable' | 'unknown'
  current: number | null; start: number | null; rate: number | null; threshold: number
  span_seconds: number; sample_count: number; exceeded: boolean; min: number | null; max: number | null
}
export interface TrendReport {
  id: number; checked_at: string; status: 'normal' | 'abnormal' | 'partial' | 'insufficient' | 'unavailable'
  message: string; channels: Record<string, TrendChannel>; event_ids: number[]; stale: boolean
  data_source: 'demo' | 'telemetry'; analysis: WarningEvent['analysis']
}
export interface WarningState {
  window_seconds?: number
  enabled: boolean; rates: { temp: number; humidity: number }; active_count: number; events: WarningEvent[]
  metrics: Record<string, { span_seconds: number; rate: number | null }>
  manual?: TrendReport | null
}
export const initialAISettings = (): AISettings => ({ provider: 'deepseek', mode: 'api', connection: { status: 'idle', message: '' }, profiles: {
  deepseek: { base_url: 'https://api.deepseek.com', model: 'deepseek-flash' },
  kimi: { base_url: 'https://api.moonshot.cn/v1', model: 'kimi-k3' }, custom: { base_url: '', model: '' },
} })
export const initialWarnings = (): WarningState => ({ enabled: false, window_seconds: 120, rates: { temp: 0.4, humidity: 1 }, active_count: 0, events: [], metrics: {} })
export interface Snapshot {
  revision: number
  demo: boolean
  connected: boolean
  port: string
  ports: string[]
  controls: { read_enabled: boolean; fan_enabled: boolean; window_enabled: boolean }
  telemetry: {
    values: Record<Field, string>
    sounds: { sound_rms_1: string; sound_rms_2: string }
    rain: { state: 0 | 1 | null; source: 'master' | null }
    mq2: { valid: boolean; raw: number | null; pa7_mv: number | null; ao_mv: number | null; age_ms: number | null }
    ultrasonic: UltrasonicReading
    slave_link: string
    updated_at: string | null
    sample_id: number
  }
  fans: FanState[]
  window: { busy: boolean; status: string }
  windows: ServoState[]
  logs: LogEntry[]
  last_log_id: number
  notice: { id: number; title: string; message: string } | null
  warnings: WarningState
  ai_settings: AISettings
}
export interface Accepted { accepted: boolean }
export interface DesktopAPI {
  refresh_ports(): Promise<Accepted>
  connect(port: string): Promise<Accepted>
  disconnect(): Promise<Accepted>
  read_once(): Promise<Accepted>
  set_fan(channel: number, duty: number | string): Promise<Accepted>
  set_window(action: number, servo_id?: number): Promise<Accepted>
  get_snapshot(after_log_id?: number): Promise<Snapshot>
  get_ai_settings?(): Promise<AISettings>
  update_ai_settings?(provider: Provider, base_url?: string, model?: string): Promise<Accepted>
  update_warning_settings?(enabled: boolean, temp_rate: number, humidity_rate: number): Promise<Accepted>
  mark_warning_read?(event_id: number): Promise<Accepted>
  analyze_warning?(event_id: number): Promise<Accepted>
  set_warning_scenario?(name: string): Promise<Accepted>
  save_ai_config?(provider: Provider, base_url: string, model: string, mode: 'api' | 'simulation', api_key?: string, remember?: boolean): Promise<Accepted>
  test_ai_connection?(): Promise<Accepted>
  monitor_trends?(): Promise<Accepted>
}

declare global {
  interface Window { pywebview?: { api?: DesktopAPI } }
}

export const initialSnapshot = (): Snapshot => ({
  revision: 0,
  demo: false,
  connected: false,
  port: '',
  ports: [],
  controls: { read_enabled: true, fan_enabled: true, window_enabled: false },
  telemetry: {
    values: {
      master_temp: '--', slave_temp: '--', master_humidity: '--', slave_humidity: '--', master_pressure: '--', slave_pressure: '--',
    },
    sounds: { sound_rms_1: '--', sound_rms_2: '--' },
    rain: { state: null, source: null },
    mq2: { valid: false, raw: null, pa7_mv: null, ao_mv: null, age_ms: null },
    ultrasonic: { valid: false, distance_mm: null, raw_mm: null, pulse_us: null, age_ms: null },
    slave_link: '未知', updated_at: null, sample_id: 0,
  },
  fans: ['PB1', 'PB8', 'PA1', 'PB9'].map((pin, index) => ({ channel: index + 1, pin, duty: 0, status: '未发送' })),
  window: { busy: false, status: '未连接，位置未知' },
  windows: ['PB8', 'PB9', 'PB10', 'PB11'].map((pin, index) => ({ channel: index + 1, pin, status: '未连接，位置未知' })),
  logs: [], last_log_id: 0, notice: null,
  warnings: initialWarnings(), ai_settings: initialAISettings(),
})
