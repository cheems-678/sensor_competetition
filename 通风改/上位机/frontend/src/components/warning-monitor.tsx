import { useEffect, useRef, useState } from 'react'
import { Button } from './button'
import { HelpDetails } from './help-details'
import { AnalysisDetails } from './analysis-details'
import { ConfirmDelete } from './confirm-delete'
import { SensorChecks } from './sensor-checks'
import { initialAISettings, initialWarnings, type Accepted, type DesktopAPI, type Provider, type Snapshot } from '../lib/types'

const providers: Record<Provider, string> = { deepseek: 'DeepSeek', kimi: 'Kimi', custom: '自定义兼容接口' }
const scenarios = { normal: '正常波动', warming: '持续升温', humidity: '持续增湿', spike: '单点尖峰', missing: '数据缺失', timeout: '通信超时', recovery: '异常恢复', reconnect: '重新连接' }
const statuses = { active: '活动', unavailable: '数据不可用', resolved: '已恢复', stopped: '已停止' }
const sources = { master: '主机', slave: '从机', link: '通信链路' }
const kinds = { temp: '温度上升', humidity: '湿度上升', communication: '通信异常', distance: '粮面距离过近', rain: '降雨提示', smoke: '烟雾相对指数超限' }

export function WarningMonitor({ snapshot, api, execute }: { snapshot: Snapshot; api: DesktopAPI | null; execute: (command: () => Promise<Accepted>) => Promise<void> }) {
  const warning = snapshot.warnings ?? initialWarnings()
  const manual = warning.manual
  const [profiles, setProfiles] = useState(() => structuredClone(snapshot.ai_settings?.profiles ?? initialAISettings().profiles))
  const [provider, setProvider] = useState<Provider>(snapshot.ai_settings?.provider ?? 'deepseek')
  const [enabled, setEnabled] = useState(warning.enabled)
  const [temp, setTemp] = useState(String(warning.rates.temp))
  const [humidity, setHumidity] = useState(String(warning.rates.humidity))
  const [scenario, setScenario] = useState('warming')
  const [formError, setFormError] = useState('')
  const [deleting, setDeleting] = useState<{ kind: 'manual' | 'event'; id: number } | null>(null)
  const [archiving, setArchiving] = useState(false)
  useEffect(() => { setArchiving(false) }, [manual?.id, manual?.archive_id])
  const [mode, setMode] = useState<'api' | 'simulation'>(snapshot.ai_settings?.mode ?? 'api')
  const [keys, setKeys] = useState<Record<Provider, string>>({ deepseek: '', kimi: '', custom: '' })
  const [remember, setRemember] = useState<Record<Provider, boolean>>({deepseek:true, kimi:true, custom:true})
  const settingsLoaded = useRef(snapshot.revision > 0)
  useEffect(() => {
    if (!settingsLoaded.current && snapshot.revision > 0) {
      setProfiles(structuredClone(snapshot.ai_settings?.profiles ?? initialAISettings().profiles))
      setProvider(snapshot.ai_settings?.provider ?? 'deepseek')
      setMode(snapshot.ai_settings?.mode ?? 'api')
      setRemember(current => Object.fromEntries(Object.entries(current).map(([p, value]) => [p, snapshot.ai_settings?.profiles[p as Provider]?.has_key ? Boolean(snapshot.ai_settings.profiles[p as Provider].remembered) : value])) as Record<Provider, boolean>)
      settingsLoaded.current = true
    }
  }, [snapshot.revision, snapshot.ai_settings])
  useEffect(() => { setEnabled(warning.enabled); setTemp(String(warning.rates.temp)); setHumidity(String(warning.rates.humidity)) }, [warning.enabled, warning.rates.temp, warning.rates.humidity])
  const profile = profiles[provider]
  const appliedProfile = snapshot.ai_settings?.profiles[provider]
  const connection = snapshot.ai_settings?.connection
  const send = (command: () => Promise<Accepted>) => { void execute(command) }
  const selectProvider = (value: Provider) => {
    setProvider(value)
    if (api?.update_ai_settings) send(() => api.update_ai_settings!(value))
  }
  const saveProfile = () => {
    const address = profile.base_url.trim()
    try {
      if (address) { const url = new URL(address); if (url.protocol !== 'https:' || url.username || url.password || url.search || url.hash) throw new Error() }
      if (!address || !profile.model.trim()) throw new Error()
      setFormError('')
      if (api?.save_ai_config) {
        const secret = keys[provider]
        const selected = provider
        send(async () => {
          const result = await api.save_ai_config!(selected, address, profile.model.trim(), mode, secret, remember[selected])
          if (result.accepted) setKeys(current => ({ ...current, [selected]: '' }))
          return result
        })
      } else if (api?.update_ai_settings) send(() => api.update_ai_settings!(provider, address, profile.model.trim()))
    } catch { setFormError('请填写不含凭据的 HTTPS 接口地址及模型 ID。') }
  }
  const updateProfile = (key: 'base_url' | 'model', value: string) => setProfiles(current => ({ ...current, [provider]: { ...current[provider], [key]: value } }))
  const applyRules = () => {
    const values = [temp, humidity].map(Number)
    if (values.some(value => !Number.isFinite(value) || value <= 0)) { setFormError('变化速度必须为有限正数。'); return }
    setFormError('')
    if (api?.update_warning_settings) send(() => api.update_warning_settings!(enabled, values[0], values[1]))
  }
  return <div className="warning-monitor">
    <section className="panel manual-trends" aria-label="主动趋势监测">
      <div className="panel-heading"><h2>主动趋势监测</h2><Button disabled={!api?.monitor_trends || manual?.analysis.status === 'pending'} onClick={() => send(() => api!.monitor_trends!())}>{manual?.analysis.status === 'pending' ? '正在分析…' : '手动趋势监测'}</Button></div>
      <p className="warning-note">点击检查当前温湿度趋势及雨滴、烟雾、测距状态并生成 AI 解释。温湿度自动预警关闭时也可用。</p>
      <div className="warning-progress">{(['master_temp', 'master_humidity', 'slave_temp', 'slave_humidity'] as const).map(key => <span key={key}>{key.startsWith('master') ? '主机' : '从机'}{key.endsWith('temp') ? '温度' : '湿度'}：{warning.metrics[key]?.rate == null ? `积累中 ${Math.floor(warning.metrics[key]?.span_seconds ?? 0)}/${warning.window_seconds ?? 120}s` : `${warning.metrics[key].rate!.toFixed(2)}${key.endsWith('temp') ? '℃' : '百分点'}/分钟`}</span>)}</div>
      {manual && <div className={`manual-result ${manual.status}`}>
        <p className="manual-conclusion" role="status">温湿度检查：{manual.message}</p>
        {manual.sensor_review && <><p className="warning-note">附加检查：{{ normal: '三项均未发现预警条件', abnormal: '存在预警条件', partial: '部分数据不可用，不能完整判定', unavailable: '三项数据不可用，无法判定' }[manual.sensor_review.status]}</p><SensorChecks readings={manual.sensor_review.readings} /></>}
        <p className="warning-note">检查时间：{manual.checked_at} · {manual.data_source === 'demo' ? '模拟数据' : '实测数据'}{manual.stale && ' · 结果已过期，请重新检查'}</p>
        <div className="manual-channels">{Object.entries(manual.channels).map(([key, item]) => <article key={key} className={`manual-channel ${item.exceeded ? 'exceeded' : ''}`}>
          <h3>{sources[item.source]} · {item.kind === 'temp' ? '温度' : '湿度'} <span>{item.state === 'unavailable' ? '不可用' : item.direction === 'unknown' ? '尚无趋势' : {rising:'上升',falling:'下降',stable:'平稳'}[item.direction]}</span></h3>
          <p>当前：{item.current == null ? '--' : `${item.current.toFixed(2)}${item.kind === 'temp' ? '℃' : '%RH'}`}</p>
          <p>变化速度：{item.rate == null ? '--' : `${item.rate.toFixed(3)}${item.kind === 'temp' ? '℃' : '百分点'}/分钟`}{item.state === 'insufficient' && '（短时估计）'}</p>
          <p className="warning-note">有效 {Math.floor(item.span_seconds)}/{warning.window_seconds ?? 120} 秒 · {item.sample_count} 个采样 · 阈值 {item.threshold}/分钟</p>
          <p>{item.exceeded ? '上升趋势超限' : item.state === 'ready' ? '未发现上升趋势超限' : item.state === 'insufficient' ? '数据不足，尚不能完整判定' : '数据不可用，无法判定'}</p>
        </article>)}</div>
        {manual.analysis.status === 'pending' && <p className="warning-note" role="status">本地检查已完成，正在请求 AI 解释…</p>}
        {manual.analysis.status === 'error' && <p className="warning-error" role="alert">{manual.analysis.result?.error} 本地检查结果保留，可再次点击监测。</p>}
        {manual.analysis.status === 'complete' && <AnalysisDetails key={manual.id} analysis={manual.analysis} />}
        <div className="warning-actions"><Button variant="outline" disabled={!api?.delete_manual_trend || manual.analysis.status === 'pending'} onClick={() => setDeleting({ kind: 'manual', id: manual.id })}>删除</Button><Button disabled={!api?.archive_manual_trend || archiving || !!manual.archive_id || manual.analysis.status === 'pending'} onClick={() => { setArchiving(true); send(async () => { try { return await api!.archive_manual_trend!(manual.id) } finally { setArchiving(false) } }) }}>{manual.archive_id ? '已记录到档案' : archiving ? '正在记录…' : '记录到档案'}</Button></div>
      </div>}
    </section>
    <section className="panel warning-settings" aria-label="预警规则设置">
      <div className="panel-heading"><h2>自动趋势预警与判断阈值</h2><span className="device-label">{warning.enabled ? '自动预警已启用' : '自动预警已关闭'}</span></div>
      <div className="warning-form">
        <label className="warning-toggle"><input type="checkbox" checked={enabled} onChange={event => setEnabled(event.target.checked)} />启用自动温湿度趋势预警</label>
        <label>温度变化速度（℃/分钟）<input inputMode="decimal" value={temp} onChange={event => setTemp(event.target.value)} /></label>
        <label>湿度变化速度（百分点/分钟）<input inputMode="decimal" value={humidity} onChange={event => setHumidity(event.target.value)} /></label>
        <Button disabled={!api?.update_warning_settings} onClick={applyRules}>应用</Button>
      </div>
      <p className="warning-note">默认值仅为测试参数，现场使用前需按工况设置。{warning.enabled && '满两分钟有效数据后判断趋势。'}</p>
      <SensorChecks readings={warning.sensors ?? {}} />
      <p className="warning-note">三项实时监测：下雨提示；烟雾相对指数 &gt;10；粮面距离 &lt;10 cm。测距需≥11 cm连续有效3秒恢复；5 cm仍是有效下限。三项不受温湿度开关影响，不自动操作设备。</p>
      <details className="help-details warning-api"><summary>API 设置 · {snapshot.ai_settings?.mode === 'simulation' ? '本地模拟' : '真实 API'}</summary><div className="warning-form">
        <label>分析模式<select value={mode} onChange={event => setMode(event.target.value as 'api' | 'simulation')}><option value="api">真实 API</option><option value="simulation">本地模拟</option></select></label>
        <label>API 厂家<select value={provider} onChange={event => selectProvider(event.target.value as Provider)}>{Object.entries(providers).map(([id, name]) => <option key={id} value={id}>{name}</option>)}</select></label>
        <label>接口地址<input type="url" value={profile.base_url} maxLength={512} onChange={event => updateProfile('base_url', event.target.value)} placeholder="https://…" /></label>
        <label>模型 ID<input value={profile.model} maxLength={128} onChange={event => updateProfile('model', event.target.value)} placeholder="填写服务商提供的模型 ID" /></label>
        <label>API Key<input type="password" disabled={mode === 'simulation'} value={keys[provider]} maxLength={4096} onChange={event => setKeys(current => ({ ...current, [provider]: event.target.value }))} placeholder={appliedProfile?.has_key ? '已配置，留空沿用；修改地址需重新填写' : '填写当前厂家的 API Key'} autoComplete="off" /></label>
        <label className="warning-toggle"><input type="checkbox" checked={snapshot.ai_settings?.persistent === false ? false : remember[provider]} disabled={snapshot.ai_settings?.persistent === false} onChange={event => setRemember(current => ({ ...current, [provider]: event.target.checked }))} />本机加密记住密钥</label>
        <Button disabled={!api?.save_ai_config && !api?.update_ai_settings} onClick={saveProfile}>应用厂家配置</Button>
        <Button variant="outline" disabled={!api?.test_ai_connection || snapshot.ai_settings?.mode !== 'api' || !appliedProfile?.has_key || connection?.status === 'pending'} onClick={() => send(() => api!.test_ai_connection!())}>{connection?.status === 'pending' ? '正在测试…' : '测试 API'}</Button>
      </div><p className="warning-note">编辑后应用配置生效。密钥按厂家独立保存，使用当前Windows用户加密；不勾选则仅本次运行保留。测试及真实分析会向所选服务商发送请求并可能产生API费用。</p>
        <p className="warning-note">密钥状态：{appliedProfile?.has_key ? appliedProfile.remembered ? '已配置 · 本机加密记住' : '已配置 · 仅本次运行' : '未配置'}。真实分析仅提供排查建议，不自动操作设备。</p>
        {snapshot.ai_settings?.persistent === false && <p className="warning-note">演示或隔离验收模式：配置仅保留在本次运行，不写本机配置文件。</p>}
        {connection?.message && <p className={connection.status === 'error' ? 'warning-error' : 'warning-note'} role="status">{connection.message}</p>}
      </details>
      {formError && <p role="alert" className="warning-error">{formError}</p>}
      <HelpDetails><p>手动检查使用点击时数据，满两分钟且超限即可生成标注“手动检查发现”的预警；自动预警需要连续超限30秒。低于阈值80%连续60秒恢复。超过10秒数据缺口重新积累，无效值不补零。关闭自动预警不影响数据积累或手动检查。主动断开显示已停止，不代表环境恢复。最近200条事件仅保留在内存。</p></HelpDetails>
    </section>
    {snapshot.demo && <section className="panel warning-settings" aria-label="预警模拟场景"><div className="panel-heading"><h2>模拟验收</h2><span className="demo-badge">虚拟时间 · 不连接硬件</span></div><div className="warning-form"><label>模拟场景<select value={scenario} onChange={event => setScenario(event.target.value)}>{Object.entries(scenarios).map(([id, name]) => <option key={id} value={id}>{name}</option>)}</select></label><Button disabled={!api?.set_warning_scenario} onClick={() => send(() => api!.set_warning_scenario!(scenario))}>运行模拟场景</Button></div></section>}
    <section className="panel warning-list" aria-label="预警事件"><div className="panel-heading"><h2>预警事件</h2><span>{warning.active_count} 条当前预警</span></div>
      {!warning.events.length && <p className="warning-empty">暂无预警事件</p>}
      {warning.events.map(event => <article key={event.id} className={`warning-card ${event.status}`}>
        <div className="panel-heading"><h3>{sources[event.source]} · {kinds[event.kind]}</h3><span className="device-label">{statuses[event.status]} · {event.read ? '已读' : '未读'}</span></div>
        {event.trigger === 'manual' && <p className="warning-note">手动检查发现 · 未使用自动预警的30秒确认条件</p>}
        <p>{event.kind === 'communication' ? event.evidence.message : event.kind === 'rain' ? `当前检测：${event.evidence.current === 1 ? '下雨（1）' : '无雨（0）'}；降雨提示条件：状态=1。` : event.kind === 'smoke' ? `当前相对指数 ${event.evidence.current} / 100，超限条件 >${event.evidence.threshold}；非浓度百分比。` : event.kind === 'distance' ? `仓顶到粮面距离 ${event.evidence.current?.toFixed(1)} cm，过近条件 <${event.evidence.threshold} cm；≥11 cm连续有效3秒恢复。` : `当前 ${event.evidence.current?.toFixed(2)}${event.kind === 'temp' ? '℃' : '%RH'}，两分钟变化速度 ${event.evidence.rate?.toFixed(2)}${event.kind === 'temp' ? '℃' : '百分点'}/分钟，配置 ${event.evidence.threshold}/分钟。`}</p>
        {['rain', 'smoke', 'distance'].includes(event.kind) && <p className="warning-note">首次触发值：{event.evidence.trigger_value ?? event.evidence.current} {event.evidence.unit ?? 'cm'} · 最近有效检测：{event.evidence.detected_at ?? event.occurred_at}{event.status === 'unavailable' && ' · 数据不可用，以上为最后有效读数'}{event.status === 'active' && event.evidence.message && ` · ${event.evidence.message}`}</p>}
        <p className="warning-note">发生：{event.occurred_at}{event.ended_at && ` · 结束：${event.ended_at}`}</p>
        <div className="warning-actions"><Button variant="outline" disabled={!api?.analyze_warning || event.analysis.status === 'pending'} onClick={() => send(() => api!.analyze_warning!(event.id))}>{event.analysis.status === 'pending' ? '分析中…' : '分析原因'}</Button><Button variant="ghost" disabled={event.read || !api?.mark_warning_read} onClick={() => send(() => api!.mark_warning_read!(event.id))}>标记已读</Button><Button variant="outline" disabled={!api?.delete_warning} onClick={() => setDeleting({ kind: 'event', id: event.id })}>删除</Button></div>
        {event.analysis.status === 'stale' && <p className="warning-note">旧分析已过期，请重新分析。</p>}
        {event.analysis.status === 'error' && <p role="alert" className="warning-error">{event.analysis.result?.error}</p>}
        {event.analysis.status === 'complete' && <AnalysisDetails analysis={event.analysis} />}
      </article>)}
    </section>
    <section className="panel warning-archives" aria-label="趋势档案"><div className="panel-heading"><h2>趋势档案</h2><span className="device-label">{warning.archive_persistent ? '本机保存 · 最近200份' : '本次运行 · 最近200份'}</span></div>
      {warning.archive_error && <p role="alert" className="warning-error">{warning.archive_error}</p>}
      {!warning.archives?.length && <p className="warning-empty">暂无档案，手动检查完成后可记录。</p>}
      {warning.archives?.map(report => <details key={report.archive_id} className="archive-report"><summary>{report.checked_at} · {report.data_source === 'demo' ? '模拟' : '实测'} · {report.message}</summary><p>{report.stale ? '归档时报告已过期' : '点击时的本地检查结果'}</p><div className="manual-channels">{Object.entries(report.channels).map(([key, channel]) => <div key={key}><h3>{sources[channel.source]} · {channel.kind === 'temp' ? '温度' : '湿度'}</h3><p>{channel.direction === 'unknown' ? '尚无趋势' : { rising: '上升', falling: '下降', stable: '平稳' }[channel.direction]} · {channel.rate?.toFixed(3) ?? '--'} {channel.kind === 'temp' ? '℃' : '百分点'}/分钟</p><p>当前 {channel.current?.toFixed(2) ?? '--'} · 有效 {Math.floor(channel.span_seconds)} 秒 · {channel.sample_count} 样本 · 阈值 {channel.threshold}/分钟</p><p>{channel.exceeded ? '上升趋势超限' : channel.state === 'ready' ? '未发现超限' : '数据不足或不可用'}</p></div>)}</div>{report.sensor_review && <SensorChecks readings={report.sensor_review.readings} />}{report.analysis.result?.summary ? <AnalysisDetails analysis={report.analysis} /> : <p className="warning-note">AI 状态：{report.analysis.status} {report.analysis.result?.error}</p>}</details>)}
    </section>
    {deleting && <ConfirmDelete message={deleting.kind === 'event' ? '确定要删除该预警事件吗？' : '确定要删除本次检测结果吗？'} cancel={() => setDeleting(null)} confirm={() => { const target = deleting; setDeleting(null); if (target.kind === 'event' && api?.delete_warning) send(() => api.delete_warning!(target.id)); else if (target.kind === 'manual' && api?.delete_manual_trend) send(() => api.delete_manual_trend!(target.id)) }} />}
  </div>
}
