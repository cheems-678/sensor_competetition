import { CIS_LOGO_DATA } from '../lib/cis-logo'

export function OrganizerBrand() {
  return <div className="organizer-brand" aria-label="主办单位：中国仪器仪表学会">
    <span className="organizer-label">主办单位</span>
    <img src={CIS_LOGO_DATA} width={266} height={57} alt="中国仪器仪表学会 · CHINA INSTRUMENT AND CONTROL SOCIETY" />
  </div>
}
