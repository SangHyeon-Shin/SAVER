/* 빌드 결과(JS·CSS)를 index.html 하나로 합칩니다.
   이미지는 SINGLE=1 빌드에서 이미 base64로 인라인되어 있으므로,
   결과 파일은 인터넷·서버 없이 더블클릭만으로 열립니다.
   사용:  npm run build:single   →  dist-single/SAVER_UI.html          */
import { readFileSync, writeFileSync, rmSync, readdirSync } from 'node:fs'
import { join } from 'node:path'

const DIR = 'dist-single'
let html = readFileSync(join(DIR, 'index.html'), 'utf8')

const assets = readdirSync(join(DIR, 'assets'))
const js = assets.find((f) => f.endsWith('.js'))
const css = assets.find((f) => f.endsWith('.css'))

if (css) {
  const code = readFileSync(join(DIR, 'assets', css), 'utf8')
  // 치환은 반드시 함수로 — 번들 코드의 $& $' 등이 특수문자로 해석되는 것을 막음
  html = html.replace(new RegExp(`\\s*<link[^>]*${css}[^>]*>`), () => `\n    <style>\n${code}\n    </style>`)
}
if (js) {
  const code = readFileSync(join(DIR, 'assets', js), 'utf8')
  // head의 <script src> 태그는 제거하고, </body> 직전에 인라인으로 삽입한다.
  // classic script는 defer가 먹지 않으므로 head에 두면 #root가 없는 상태에서 실행돼 흰/검은 화면이 된다.
  html = html.replace(new RegExp(`\\s*<script[^>]*${js}[^>]*></script>`), '')
  html = html.replace('</body>', () => `  <script>\n${code}\n  </script>\n  </body>`)
}

if (/<(script|link)[^>]*assets\//.test(html)) {
  console.error('❌ 인라인 실패 — assets 참조가 남아 있습니다')
  process.exit(1)
}
if (html.indexOf('<div id="root">') > html.indexOf('<script>\n')) {
  console.error('❌ 스크립트가 #root 보다 앞에 있습니다')
  process.exit(1)
}

const out = join(DIR, 'SAVER_UI.html')
writeFileSync(out, html)
rmSync(join(DIR, 'assets'), { recursive: true, force: true })
rmSync(join(DIR, 'index.html'), { force: true })

const kb = (readFileSync(out).length / 1024).toFixed(0)
console.log(`✅ ${out}  (${kb} KB) — 인터넷 없이 더블클릭으로 열립니다`)
