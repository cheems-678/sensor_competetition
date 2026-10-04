import { useEffect, useId, useRef, useState } from 'react'
import { Check, ChevronDown } from 'lucide-react'

export function PortCombobox({ value, ports, onChange, ready }: { value: string; ports: string[]; onChange: (value: string) => void; ready: boolean }) {
  const [open, setOpen] = useState(false)
  const [active, setActive] = useState(-1)
  const rootRef = useRef<HTMLDivElement>(null)
  const inputRef = useRef<HTMLInputElement>(null)
  const listId = useId()
  useEffect(() => {
    if (!open) return
    const outside = (event: PointerEvent) => {
      if (event.target instanceof Node && !rootRef.current?.contains(event.target)) setOpen(false)
    }
    document.addEventListener('pointerdown', outside)
    return () => document.removeEventListener('pointerdown', outside)
  }, [open])
  const choose = (port: string) => { onChange(port); setOpen(false); inputRef.current?.focus() }
  const show = () => { setActive(Math.max(0, ports.indexOf(value))); setOpen(true); inputRef.current?.focus() }
  return <div className="port-field" ref={rootRef} onKeyDown={event => { if (event.key === 'Escape') { setOpen(false); inputRef.current?.focus() } }}>
    <input
      ref={inputRef} aria-label="串口端口" role="combobox" aria-expanded={open}
      aria-controls={open ? listId : undefined} aria-haspopup="listbox" aria-autocomplete="none"
      aria-activedescendant={open && active >= 0 && active < ports.length ? `${listId}-${active}` : undefined}
      value={value} placeholder="选择或输入端口" onChange={event => onChange(event.target.value)}
      onKeyDown={event => {
        if (event.key === 'Escape') { setOpen(false); return }
        if (event.key === 'Tab') { setOpen(false); return }
        if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
          if (!ready) return
          event.preventDefault()
          if (!open) show()
          else if (ports.length) setActive(index => (index + (event.key === 'ArrowDown' ? 1 : -1) + ports.length) % ports.length)
        } else if (event.key === 'Enter' && open && ports[active]) {
          event.preventDefault()
          choose(ports[active])
        }
      }}
    />
    <button type="button" className="port-toggle" aria-label={open ? '收起串口列表' : '展开串口列表'} aria-haspopup="listbox" aria-expanded={open} disabled={!ready} onClick={() => open ? setOpen(false) : show()}><ChevronDown size={16} /></button>
    {open && <div className="port-dropdown" role="listbox" id={listId} aria-label="检测到的串口">
      {ports.length ? ports.map((port, index) => <button type="button" role="option" aria-selected={value === port} className={`port-option ${active === index ? 'active' : ''}`} id={`${listId}-${index}`} key={port} onMouseDown={event => event.preventDefault()} onClick={() => choose(port)} onMouseEnter={() => setActive(index)}><span>{port}</span>{value === port && <Check size={14} />}</button>) : <div className="ports-empty">未检测到可用串口</div>}
    </div>}
  </div>
}
