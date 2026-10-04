export interface LogEntry { id: number; text: string }
export interface FanState { channel: number; pin: string; duty: number; status: string }
export type Field = 'master_temp' | 'slave_temp' | 'master_humidity' | 'slave_humidity' | 'master_pressure' | 'slave_pressure'
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
    slave_link: string
    updated_at: string | null
  }
  fans: FanState[]
  window: { busy: boolean; status: string }
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
  set_window(action: number): Promise<Accepted>
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
    slave_link: '未知', updated_at: null,
  },
  fans: [{ channel: 1, pin: 'PB1', duty: 0, status: '未发送' }, { channel: 2, pin: 'PB8', duty: 0, status: '未发送' }],
  window: { busy: false, status: '未连接，位置未知' },
  logs: [], last_log_id: 0, notice: null,
})
