import { simulatedTemperature, temperatureColor } from './simulated-temperature'

type Point = readonly [number, number, number]
type Color = readonly [number, number, number]
type Face = { points: Point[]; color: Color; edge?: boolean; thermal?: { height: number; angle: number } }
type Segment = { points: [Point, Point]; color: Color; width: number }
export type GranaryView = { yaw: number; pitch: number; zoom: number }
export const defaultGranaryView = (): GranaryView => ({ yaw: -.55, pitch: .24, zoom: 1 })

export function adjustGranaryView(view: GranaryView, yaw = 0, pitch = 0, factor = 1): GranaryView {
  return {
    yaw: (view.yaw + yaw) % (Math.PI * 2),
    pitch: Math.max(-.15, Math.min(.85, view.pitch + pitch)),
    zoom: Math.max(.65, Math.min(1.55, view.zoom * factor)),
  }
}

const steel: Color = [160, 185, 203]
const trim: Color = [98, 135, 164]
const grain: Color = [193, 151, 77]
const faces: Face[] = []
const segments: Segment[] = []
const circle = (angle: number, y: number, radius = .88): Point => [Math.cos(angle) * radius, y, Math.sin(angle) * radius]
const face = (points: Point[], color: Color, edge = false, thermal?: Face['thermal']) => faces.push({ points, color, edge, thermal })
const line = (a: Point, b: Point, color: Color, width = 1) => segments.push({ points: [a, b], color, width })

// Local procedural geometry. The cutaway and fill surface are fixed illustrations.
for (let i = 0; i < 64; i++) {
  const a = i / 64 * Math.PI * 2, b = (i + 1) / 64 * Math.PI * 2
  const cutaway = a >= Math.PI * 2 * 2 / 64 && a < Math.PI * 2 * 19 / 64
  face([circle(a, -1.16, 1.04), circle(b, -1.16, 1.04), circle(b, -1.28, 1.04), circle(a, -1.28, 1.04)], [54, 77, 98])
  face([[0, -1.16, 0], circle(a, -1.16, 1.04), circle(b, -1.16, 1.04)], [91, 115, 133])
  face([[0, .06, 0], circle(a, .06, .835), circle(b, .06, .835)], grain, false, { height: .06, angle: (a + b) / 2 })
  for (let row = 0; row < 10; row++) {
    const low = -.99 + row * .105, high = low + .105
    face([circle(a, low, .835), circle(b, low, .835), circle(b, high, .835), circle(a, high, .835)], grain, false, { height: (low + high) / 2, angle: (a + b) / 2 })
  }
  if (cutaway) continue
  for (let row = 0; row < 20; row++) {
    const low = -1.1 + row * .106, high = low + .106
    face([circle(a, low), circle(b, low), circle(b, high), circle(a, high)], steel, false, { height: (low + high) / 2, angle: (a + b) / 2 })
  }
  face([circle(a, 1.02, .96), circle(b, 1.02, .96), [0, 1.59, 0]], [119, 151, 176], true, { height: 1.02, angle: (a + b) / 2 })
  for (let row = 0; row < 17; row++) {
    const y = -1.06 + row * .124
    line(circle(a, y, .886), circle(b, y, .886), [60, 103, 127], .5)
  }
  line(circle(a, -1.1), circle(b, -1.1), trim, 2)
  line(circle(a, 1.02, .89), circle(b, 1.02, .89), [186, 207, 220], 2)
}
for (const angle of [Math.PI * 2 * 2 / 64, Math.PI * 2 * 19 / 64]) {
  face([circle(angle, -1.1, .84), circle(angle, -1.1), circle(angle, 1.02), circle(angle, 1.02, .84)], [204, 221, 231])
  line(circle(angle, -1.1), circle(angle, 1.02), [222, 238, 247], 2)
}
// A fixed access ladder and roof cap supply structural detail without device semantics.
for (const angle of [2.14, 2.34]) line(circle(angle, -1.12, .98), circle(angle, 1.03, .98), [197, 215, 226], 2.5)
for (let i = 0; i < 15; i++) line(circle(2.14, -.97 + i * .135, .98), circle(2.34, -.97 + i * .135, .98), [166, 192, 211], 2)
for (let i = 0; i < 32; i++) {
  const a = i / 32 * Math.PI * 2, b = (i + 1) / 32 * Math.PI * 2
  face([circle(a, 1.6, .105), circle(b, 1.6, .105), circle(b, 1.67, .105), circle(a, 1.67, .105)], trim)
  face([[0, 1.67, 0], circle(a, 1.67, .105), circle(b, 1.67, .105)], [193, 213, 226])
}

function transform(point: Point, view: GranaryView): Point {
  const x = point[0] * Math.cos(view.yaw) + point[2] * Math.sin(view.yaw)
  const z = -point[0] * Math.sin(view.yaw) + point[2] * Math.cos(view.yaw)
  const y = point[1] - .19
  return [x, y * Math.cos(view.pitch) - z * Math.sin(view.pitch), y * Math.sin(view.pitch) + z * Math.cos(view.pitch)]
}

function projectTransformed(point: Point, width: number, height: number, view: GranaryView) {
  const scale = Math.min(height * .258, width * .23) * view.zoom
  const perspective = 6 / (6 - point[2])
  return [width * .5 + point[0] * scale * perspective, height * .5 - point[1] * scale * perspective] as const
}

// Shared camera/projection keeps sound markers attached to the wall when rotating.
export function projectGranaryPoint(point: Point, width: number, height: number, view: GranaryView) {
  return projectTransformed(transform(point, view), width, height, view)
}

export function drawAcousticGranary(context: CanvasRenderingContext2D, width: number, height: number, view: GranaryView) {
  const project = (point: Point) => projectGranaryPoint(point, width, height, view)
  const path = (points: Point[], color: string, lineWidth = 1, fill?: string) => {
    const projected = points.map(project)
    context.beginPath(); context.moveTo(...projected[0])
    for (const point of projected.slice(1)) context.lineTo(...point)
    if (fill) { context.closePath(); context.fillStyle = fill; context.fill() }
    context.strokeStyle = color; context.lineWidth = lineWidth; context.stroke()
  }
  context.clearRect(0, 0, width, height)
  const glow = context.createRadialGradient(width * .5, height * .46, 0, width * .5, height * .46, height * .7)
  glow.addColorStop(0, '#102b40'); glow.addColorStop(1, '#060e1b')
  context.fillStyle = glow; context.fillRect(0, 0, width, height)
  for (let i = -4; i <= 4; i++) {
    path([[i * .4, -1.27, -1.8], [i * .4, -1.27, 1.8]], '#31516945', .6)
    path([[-1.8, -1.27, i * .4], [1.8, -1.27, i * .4]], '#31516945', .6)
  }
  for (let i = 0; i < 32; i++) {
    const a = i / 32 * Math.PI * 2, b = (i + 1) / 32 * Math.PI * 2
    path([circle(a, -1.1), circle(b, -1.1), circle(b, 1.02), circle(a, 1.02)], '#5996b018', .5, '#449fc80a')
    path([circle(a, 1.02, .96), circle(b, 1.02, .96), [0, 1.59, 0]], '#609fba20', .5, '#6fb5d00a')
    if (i % 4 === 0) path([circle(a, -1.1), circle(a, 1.02), [0, 1.59, 0]], '#79bdd252')
  }
  for (const y of [-1.1, -.57, -.04, .49, 1.02]) {
    path(Array.from({ length: 65 }, (_, i) => circle(i / 64 * Math.PI * 2, y)), '#6baecb65', y === 1.02 || y === -1.1 ? 1.4 : .6)
  }
  context.setLineDash([4, 5])
  path(Array.from({ length: 65 }, (_, i) => circle(i / 64 * Math.PI * 2, .45, .86)), '#3bced988', 1.2)
  context.setLineDash([])
}

export function drawGranary(context: CanvasRenderingContext2D, width: number, height: number, view: GranaryView, seconds = 0) {
  const scale = Math.min(height * .258, width * .23) * view.zoom
  const project = (p: Point) => {
    return projectTransformed(p, width, height, view)
  }
  context.clearRect(0, 0, width, height)
  const glow = context.createRadialGradient(width * .5, height * .48, 0, width * .5, height * .48, height * .85)
  glow.addColorStop(0, '#122d43'); glow.addColorStop(1, '#060e1b')
  context.fillStyle = glow; context.fillRect(0, 0, width, height)

  context.strokeStyle = '#89b0cc18'; context.lineWidth = .7
  for (let i = -6; i <= 6; i++) {
    for (const points of [ [[i * .4, -1.29, -2.4], [i * .4, -1.29, 2.4]], [[-2.4, -1.29, i * .4], [2.4, -1.29, i * .4]] ] as Point[][]) {
      const a = project(transform(points[0], view)), b = project(transform(points[1], view))
      context.beginPath(); context.moveTo(a[0], a[1]); context.lineTo(b[0], b[1]); context.stroke()
    }
  }
  const shadow = project(transform([0, -1.29, 0], view))
  const shade = context.createRadialGradient(shadow[0], shadow[1], 0, shadow[0], shadow[1], scale * 1.25)
  shade.addColorStop(0, '#03081199'); shade.addColorStop(1, '#03081100')
  context.fillStyle = shade
  context.beginPath(); context.ellipse(shadow[0], shadow[1], scale * 1.4, scale * .27, 0, 0, Math.PI * 2); context.fill()

  const primitives = [
    ...faces.map(f => ({ face: f, points: f.points.map(p => transform(p, view)), segment: undefined })),
    ...segments.map(s => ({ face: undefined, points: s.points.map(p => transform(p, view)), segment: s })),
  ].sort((a, b) => a.points.reduce((sum, p) => sum + p[2], 0) / a.points.length - b.points.reduce((sum, p) => sum + p[2], 0) / b.points.length)
  for (const primitive of primitives) {
    const points = primitive.points.map(project)
    context.beginPath(); context.moveTo(points[0][0], points[0][1])
    for (const p of points.slice(1)) context.lineTo(p[0], p[1])
    if (primitive.face) {
      const [a, b, c] = primitive.points
      const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]], v = [c[0] - a[0], c[1] - a[1], c[2] - a[2]]
      const normal = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
      const length = Math.hypot(...normal) || 1
      const light = .68 + .32 * Math.abs((-normal[0] * .35 + normal[1] * .8 + normal[2] * .4) / length)
      const thermal = primitive.face.thermal
      const color = thermal ? temperatureColor(simulatedTemperature(thermal.height, seconds, thermal.angle)) : primitive.face.color
      context.closePath()
      context.fillStyle = `rgb(${color.map(value => Math.round(value * light)).join(',')})`
      context.fill()
      if (primitive.face.edge) { context.strokeStyle = '#c9dfed35'; context.lineWidth = .6; context.stroke() }
    } else if (primitive.segment) {
      context.strokeStyle = `rgb(${primitive.segment.color.join(',')})`
      context.lineWidth = primitive.segment.width; context.stroke()
    }
  }
}
