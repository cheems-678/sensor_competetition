import { useEffect, useRef, useState } from 'react'
import { connectBridge } from './bridge'
import { initialSnapshot, type DesktopAPI, type LogEntry, type Snapshot } from './types'

export function mergeLogs(current: LogEntry[], incoming: LogEntry[]): LogEntry[] {
  const present = new Set(current.map(log => log.id))
  const added = incoming.filter(log => !present.has(log.id) && present.add(log.id))
  return added.length ? [...current, ...added] : current
}

export function useMonitor(providedAPI?: DesktopAPI) {
  const [api, setAPI] = useState<DesktopAPI | null>(null)
  const [snapshot, setSnapshot] = useState<Snapshot>(initialSnapshot)
  const [logs, setLogs] = useState<LogEntry[]>([])
  const [error, setError] = useState('')
  const lastLog = useRef(0)
  useEffect(() => {
    let disposed = false
    let timer: ReturnType<typeof setTimeout> | undefined
    const start = async () => {
      try {
        const bridge = providedAPI ?? await connectBridge()
        if (disposed) return
        setAPI(bridge)
        await bridge.refresh_ports()
        const poll = async () => {
          try {
            const next = await bridge.get_snapshot(lastLog.current)
            if (disposed) return
            lastLog.current = Math.max(lastLog.current, next.last_log_id)
            setSnapshot(current => current.revision === next.revision ? current : next)
            setLogs(current => mergeLogs(current, next.logs))
            setError('')
          } catch (reason) {
            if (!disposed) setError(`无法读取界面状态：${reason instanceof Error ? reason.message : String(reason)}`)
          }
          if (!disposed) timer = setTimeout(() => void poll(), 100)
        }
        await poll()
      } catch (reason) {
        if (!disposed) setError(`界面连接失败：${reason instanceof Error ? reason.message : String(reason)}`)
      }
    }
    void start()
    return () => { disposed = true; if (timer) clearTimeout(timer) }
  }, [providedAPI])
  const execute = async (command: () => Promise<{ accepted: boolean }>) => {
    try {
      const result = await command()
      if (!result.accepted) setError('操作未提交，请等待当前请求结束后再试。')
    } catch (reason) { setError(`操作未提交：${reason instanceof Error ? reason.message : String(reason)}`) }
  }
  return { api, snapshot, logs, error, execute }
}
