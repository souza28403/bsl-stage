import { writeFileSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { pathToFileURL } from 'node:url'

const [studio, out] = process.argv.slice(2)
if (!studio || !out) {
  console.error('uso: node gen_webstudio_data.mjs <BSL_Background_Studio> <saida.json>')
  process.exit(2)
}
const c = await import(pathToFileURL(resolve(join(studio, 'src/shared/contract.js'))).href)
const data = {
  version: 1,
  backgrounds: c.BACKGROUNDS,
  params: Object.fromEntries(c.BACKGROUNDS.map(b => [b.id, c.paramsFor(b.id).map(p => ({
    id: p.id, label: p.label, type: p.type || 'number', min: p.min, max: p.max, step: p.step, default: p.default,
    options: p.options, showIf: p.showIf,
  }))])),
  palettes: c.PALETTES,
  fonts: c.FONTS,
  textIn: c.TEXT_IN,
  textLoop: c.TEXT_LOOP,
  textOut: c.TEXT_OUT,
  positions: c.POSITIONS,
  defaults: c.DEFAULT_STATE,
}
writeFileSync(out, JSON.stringify(data))
console.log('ok', out)
