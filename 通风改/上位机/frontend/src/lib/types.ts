export interface LogEntry { id: number; text: string }
export interface ServoState { channel: number; pin: string; status: string }
export interface FanState { channel: number; pin: string; duty: number; status: string }
export type Field = 'master_temp' | 'slave_temp' | 'master_humidity' | 'slave_humidity' | 'master_pressure' | 'slave_pressure'
export interface UltrasonicReading { valid: boolean; distance_mm: number | null; raw_mm: number | null; pulse_us: number | null; age_ms: number | null }
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
})
