import { useState } from 'react'
import { Waves } from 'lucide-react'
import { Button } from './button'
import { HelpDetails } from './help-details'
import { AcousticModel } from './acoustic-model'
import { acousticChannels, acousticReading, parseAcousticThresholds } from '../lib/acoustic'
import type { Snapshot } from '../lib/types'

export function AcousticMonitor({ sounds, connected, active }: { sounds: Snapshot['telemetry']['sounds']; connected: boolean; active: boolean }) {
  const [drafts, setDrafts] = useState<string[]>(() => Array(5).fill(''))
  const [thresholds, setThresholds] = useState<(number | null)[]>(() => Array(5).fill(null))
  const [message, setMessage] = useState('')
  const [error, setError] = useState('')
  const readings = acousticChannels.map((_, index) => acousticReading(sounds[`sound_p2p_${index + 1}` as 'sound_p2p_1'], thresholds[index], connected))
  const apply = () => {
    const parsed = parseAcousticThresholds(drafts)
    setError(parsed.error)
    if (parsed.error) { setMessage(''); return }
    setThresholds(parsed.values); setMessage('阈值已应用 · 仅本次运行有效')
  }
  return <div className="acoustic-monitor">
    <AcousticModel readings={readings} active={active} />
    <section className="panel acoustic-panel" aria-labelledby="acoustic-title">
      <div className="panel-heading"><div className="heading-name"><Waves size={18} strokeWidth={1.6} /><h2 id="acoustic-title">主机声音 · MAX4466</h2></div><span className="device-label">五路<span className="device-divider" />峰峰值</span></div>
      <div className="sounds-grid">{acousticChannels.map((pin, index) => {
        const reading = readings[index]
        return <div className={`sound-metric acoustic-channel ${reading.state}`} key={pin} data-channel={index + 1} data-state={reading.state}>
          <span className="metric-label">声音 {index + 1} · {pin}</span>
          <div className="sound-value">{reading.value ?? '--'}<span>ADC计数</span></div>
          <span className={`acoustic-channel-state ${reading.state}`} role="status" aria-label={`声音 ${index + 1} 状态`}>{reading.label}</span>
          <label className="acoustic-threshold">幅度阈值<input type="text" inputMode="numeric" placeholder="留空禁用" aria-label={`声音 ${index + 1} 阈值`} value={drafts[index]}
            onChange={event => { const text = event.target.value; setDrafts(current => current.map((value, i) => i === index ? text : value)); setMessage(''); setError('') }} /></label>
          <span className="acoustic-applied">已应用：{thresholds[index] === null ? '未设置' : thresholds[index]}</span>
        </div>
      })}</div>
      <div className="acoustic-threshold-actions"><Button variant="outline" onClick={apply}>应用阈值</Button>{error ? <span className="warning-error" role="alert">{error}</span> : <span role="status">{message || '留空不判断 · 修改后需应用'}</span>}</div>
      <HelpDetails><p>主机连续扫描五路，显示最新约54 ms短窗的最大值减最小值，界面约每秒更新。单位是12位ADC计数，不是分贝；不保留历史最大值。声音与从机在线无关，未知显示 --，静音可为0。模块使用3.3 V并共地，五路增益需分别校准。</p>
        <p>每路阈值为0～4095整数，留空禁用；应用后生效，切页/断开重连保留，软件重启清空。读数大于阈值闪红，等于或小于恢复；无数据不判断正常。点位仅为内壁均匀安装示意，不识别声音类型或定位声源，约每秒上传可能漏掉短促声音。不触发智能预警、AI或设备控制。</p>
        <p>拖拽旋转、滚轮缩放，方向键旋转，+/−缩放，Home复位。后侧点位始终可见；隐藏/离屏停止闪烁，减少动态效果时超限常亮红。</p></HelpDetails>
    </section>
  </div>
}
