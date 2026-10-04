import { createRoot } from 'react-dom/client'
import { App } from './App'
import './styles.css'

const query = new URLSearchParams(window.location.search)
if (query.get('preview') === '1' && !query.has('desktop') && !window.pywebview) {
  const scale = Number(new URLSearchParams(window.location.search).get('scale'))
  if (scale >= 1 && scale <= 2) document.documentElement.style.setProperty('--font-scale', String(scale))
}

createRoot(document.getElementById('root')!).render(<App />)
