import { memo, useState } from 'react'
import { Activity } from 'lucide-react'
import { HelpDetails } from './help-details'
import { HISTORY_WINDOW_MS, linePath, type EnvironmentPoint } from '../lib/environment-history'
import type { Field } from '../lib/types'

const metrics = [
  { id: 'temp', label: '温度', unit: '°C', padding: .5 },
  { id: 'humidity', label: '湿度', unit: '%', padding: 1 },
  { id: 'pressure', label: '气压', unit: 'Pa', padding: 10 },
] as const
const sides = [{ id: 'master', label: '主机' }, { id: 'slave', label: '从机' }] as const
const stamp = (at: number) => new Date(at).toLocaleTimeString('zh-CN', { hour12: false })
const tickLabel = (value: number) => Math.abs(value) >= 1e6 ? value.toExponential(1) : Number(value.toFixed(2)).toLocaleString('zh-CN')

export const EnvironmentTrend = memo(function EnvironmentTrend({ points, connected }: { points: EnvironmentPoint[]; connected: boolean }) {
  const [metricId, setMetricId] = useState<typeof metrics[number]['id']>('temp')
  const [selectedAt, setSelectedAt] = useState<number | null>(null)
  const metric = metrics.find(item => item.id === metricId)!
  const fields = sides.map(side => `${side.id}_${metricId}` as Field)
  const values = points.flatMap(point => fields.map(field => point.values[field])).filter((value): value is number => value !== null)
  const available = values.length > 0
  const end = points.at(-1)?.at ?? Date.now()
  const start = Math.max(end - HISTORY_WINDOW_MS, Math.min(points[0]?.at ?? end, end - 30_000))
  const minimum = available ? Math.min(...values) : 0
  const maximum = available ? Math.max(...values) : 1
  const padding = Math.max((maximum - minimum) * .12, metric.padding)
  const bottom = minimum - padding, top = maximum + padding
  const plotLeft = 112, plotWidth = 714
  const x = (at: number) => plotLeft + (at - start) / (end - start) * plotWidth
  const y = (value: number) => 238 - (value - bottom) / (top - bottom) * 206
  const selected = selectedAt === null ? points.at(-1) : points.find(point => point.at === selectedAt) ?? points.at(-1)
  const selectNearest = (target: number) => {
    if (!points.length) return
    setSelectedAt(points.reduce((closest, point) => Math.abs(point.at - target) < Math.abs(closest.at - target) ? point : closest).at)
  }
  return <section className="panel environment-trend" aria-labelledby="trend-title">
    <div className="trend-heading"><div className="heading-name"><Activity size={18} /><h2 id="trend-title">环境趋势</h2></div>
      <div className="trend-selector" role="group" aria-label="曲线指标">{metrics.map(item => <button key={item.id} type="button" aria-pressed={metricId === item.id} onClick={() => { setMetricId(item.id); setSelectedAt(null) }}>{item.label}</button>)}</div>
    </div>
    <div className="trend-meta"><span>{metric.label} / {metric.unit}</span><div className="trend-legend">{sides.map(side => <span key={side.id}><i className={side.id} />{side.label}</span>)}</div><span className="trend-connection">{connected ? '最近 10 分钟' : points.length ? '已断开' : '未连接'}</span></div>
    <div className="trend-stage">
      <svg viewBox="0 0 860 286" className="trend-chart" role="img" aria-label={`主从机${metric.label}变化曲线`} tabIndex={available ? 0 : -1}
        onPointerMove={event => { const rect = event.currentTarget.getBoundingClientRect(); selectNearest(start + ((event.clientX - rect.left) / rect.width * 860 - plotLeft) / plotWidth * (end - start)) }}
        onPointerLeave={() => setSelectedAt(null)} onBlur={() => setSelectedAt(null)}
        onKeyDown={event => { if (!points.length || !['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return; event.preventDefault(); const index = selected ? points.indexOf(selected) : points.length - 1; const next = event.key === 'Home' ? 0 : event.key === 'End' ? points.length - 1 : Math.max(0, Math.min(points.length - 1, index + (event.key === 'ArrowLeft' ? -1 : 1))); setSelectedAt(points[next].at) }}>
        {Array.from({ length: 5 }, (_, index) => { const value = bottom + index / 4 * (top - bottom), height = y(value); return <g key={index} className="trend-grid"><line x1={plotLeft} x2="826" y1={height} y2={height} />{available && <text x={plotLeft - 12} y={height + 4} textAnchor="end">{tickLabel(metricId === 'pressure' ? Math.round(value) : Number(value.toFixed(1)))}</text>}</g> })}
        {points.length > 0 && Array.from({ length: 4 }, (_, index) => { const at = start + index / 3 * (end - start); return <text className="trend-time" key={index} x={x(at)} y="267" textAnchor={index === 0 ? 'start' : index === 3 ? 'end' : 'middle'}>{stamp(at)}</text> })}
        {available && sides.map((side, index) => <g key={side.id} className={`trend-series ${side.id}`}>
          <path d={linePath(points, fields[index], x, y)} fill="none" vectorEffect="non-scaling-stroke" />
          {points.map((point, i) => point.values[fields[index]] !== null && (i === 0 || i === points.length - 1 || points[i - 1].values[fields[index]] === null || points[i + 1]?.values[fields[index]] === null) ? <circle key={point.at} cx={x(point.at)} cy={y(point.values[fields[index]]!)} r="3" /> : null)}
        </g>)}
        {available && selected && <g className="trend-cursor"><line x1={x(selected.at)} x2={x(selected.at)} y1="32" y2="238" />{fields.map((field, index) => selected.values[field] !== null && <circle key={field} className={sides[index].id} cx={x(selected.at)} cy={y(selected.values[field]!)} r="4" />)}</g>}
      </svg>
      {!available && <div className="trend-empty" role="status">{points.length ? `暂无有效${metric.label}数据` : connected ? '等待有效遥测' : '连接后显示曲线'}</div>}
    </div>
    <div className="trend-reading" aria-label="曲线读数"><time>{selected ? stamp(selected.at) : '--'}</time>{sides.map((side, index) => <span key={side.id} className={side.id}>{side.label}<strong>{selected?.values[fields[index]] == null ? '--' : selected.values[fields[index]]!.toLocaleString('zh-CN', { maximumFractionDigits: metricId === 'pressure' ? 0 : 1 })}</strong>{metric.unit}</span>)}</div>
    <HelpDetails><p>记录本次连接最近 10 分钟的遥测，横轴为上位机接收时间；最多保留 1200 个观测点。无效数据留断点，断线保留最后曲线，重连清空；不读取历史数据库。</p><p>鼠标移到曲线上查看读数；聚焦曲线后用左右方向键切换观测点，Home/End 查看首末点。</p></HelpDetails>
  </section>
})
