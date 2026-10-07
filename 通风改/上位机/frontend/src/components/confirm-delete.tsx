import { useEffect, useRef } from 'react'
import { Button } from './button'

export function ConfirmDelete({ message, cancel, confirm }: { message: string; cancel: () => void; confirm: () => void }) {
  const ref = useRef<HTMLDivElement>(null)
  const cancelRef = useRef<HTMLButtonElement>(null)
  useEffect(() => {
    const previous = document.activeElement as HTMLElement | null
    cancelRef.current?.focus()
    return () => previous?.focus()
  }, [])
  return <div className="confirm-overlay" onClick={event => { if (event.target === event.currentTarget) cancel() }}><div ref={ref} className="confirm-dialog" role="alertdialog" aria-modal="true" aria-label={message} onKeyDown={event => {
    if (event.key === 'Escape') { event.preventDefault(); cancel() }
    if (event.key === 'Tab') {
      const buttons = ref.current?.querySelectorAll<HTMLButtonElement>('button')
      if (!buttons?.length) return
      if (event.shiftKey && document.activeElement === buttons[0]) { event.preventDefault(); buttons[buttons.length - 1].focus() }
      else if (!event.shiftKey && document.activeElement === buttons[buttons.length - 1]) { event.preventDefault(); buttons[0].focus() }
    }
  }}><h3>{message}</h3><div className="warning-actions"><Button ref={cancelRef} variant="outline" onClick={cancel}>取消</Button><Button className="delete-button" onClick={confirm}>删除</Button></div></div></div>
}
