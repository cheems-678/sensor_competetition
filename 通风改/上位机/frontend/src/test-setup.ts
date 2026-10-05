import '@testing-library/jest-dom/vitest'

Element.prototype.scrollIntoView = () => undefined
// jsdom has no drawing backend; renderer interactions supply their own canvas stub.
HTMLCanvasElement.prototype.getContext = (() => null) as typeof HTMLCanvasElement.prototype.getContext
