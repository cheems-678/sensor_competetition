import { useId, useState } from 'react'
import type { WarningEvent } from '../lib/types'

export function AnalysisDetails({ analysis }: { analysis: WarningEvent['analysis'] }) {
  const [open, setOpen] = useState(true)
  const id = useId()
  return <div className="warning-analysis">
    <div className="analysis-heading"><strong>{analysis.mode === 'api' ? analysis.data_source === 'demo' ? '真实 API 分析 · 输入为模拟数据' : '真实 API 分析' : '模拟解释，未调用 API'}</strong><button type="button" aria-expanded={open} aria-controls={id} onClick={() => setOpen(value => !value)}>{open ? '收起分析' : '展开分析'}</button></div>
    <div id={id} className={`analysis-fold ${open ? 'expanded' : ''}`} aria-hidden={!open}><div>
      <p>{analysis.result?.summary}</p><p>可能原因：{analysis.result?.possible_causes?.join('；') || '未提供额外原因'}</p><p>检查建议：{analysis.result?.suggested_checks?.join('；')}</p><p className="warning-note">{analysis.result?.limitations}</p><p className="warning-note">{analysis.provider} / {analysis.model} · {analysis.analyzed_at}</p>
    </div></div>
  </div>
}
