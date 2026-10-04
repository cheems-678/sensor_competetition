import { BrowserDemo } from './demo'
import type { DesktopAPI } from './types'

const apiMethods = ['refresh_ports', 'connect', 'disconnect', 'read_once', 'set_fan', 'set_window', 'get_snapshot'] as const
function readyAPI(): DesktopAPI | undefined {
  const api = window.pywebview?.api
  return api && apiMethods.every(method => typeof api[method] === 'function') ? api : undefined
}

export function connectBridge(): Promise<DesktopAPI> {
  const api = readyAPI()
  if (api) return Promise.resolve(api)
  const query = new URLSearchParams(window.location.search)
  if (!window.pywebview && !query.has('desktop')) {
    if (query.get('preview') === '1') return Promise.resolve(new BrowserDemo())
    return Promise.reject(new Error('请打开智能通风系统_正式版.exe 使用真实串口'))
  }
  return new Promise((resolve) => {
    const ready = () => {
      const api = readyAPI()
      if (!api) return
      window.removeEventListener('pywebviewready', ready)
      resolve(api)
    }
    window.addEventListener('pywebviewready', ready)
    ready()
  })
}
