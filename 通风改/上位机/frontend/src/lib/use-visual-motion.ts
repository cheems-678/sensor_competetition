import { useEffect, useRef, useState } from 'react'

export function useVisualMotion(active: boolean) {
  const ref = useRef<HTMLElement | null>(null)
  const [visible, setVisible] = useState(!document.hidden)
  const [intersecting, setIntersecting] = useState(true)
  const [reduced, setReduced] = useState(() => window.matchMedia?.('(prefers-reduced-motion: reduce)').matches ?? false)
  useEffect(() => {
    const media = window.matchMedia?.('(prefers-reduced-motion: reduce)')
    const visibility = () => setVisible(!document.hidden)
    const preference = () => setReduced(media?.matches ?? false)
    document.addEventListener('visibilitychange', visibility)
    media?.addEventListener?.('change', preference)
    const observer = typeof IntersectionObserver === 'undefined' ? null : new IntersectionObserver(entries => setIntersecting(entries[0]?.isIntersecting ?? false))
    if (ref.current) observer?.observe(ref.current)
    return () => { document.removeEventListener('visibilitychange', visibility); media?.removeEventListener?.('change', preference); observer?.disconnect() }
  }, [])
  return { ref, running: active && visible && intersecting && !reduced, reduced }
}
