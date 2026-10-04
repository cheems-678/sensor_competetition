// Normalize ordinary decimal drafts only. Other text is passed unchanged to
// Python's existing validation, so JS Number('0x10') cannot bypass that rule.
export function decimalDraft(raw: string): number {
  const value = raw.trim()
  if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(value)) return NaN
  return Number(value)
}
