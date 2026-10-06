import type { ReactNode } from 'react'

export function HelpDetails({ children, id }: { children: ReactNode; id?: string }) {
  return <details className="help-details" id={id}>
    <summary>说明</summary>
    <div className="help-content">{children}</div>
  </details>
}
