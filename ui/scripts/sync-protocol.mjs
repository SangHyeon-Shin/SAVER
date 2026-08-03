/* protocol/state_machine_v2.json 을 UI 빌드에 쓸 수 있도록 복사합니다.
   Vite 는 프로젝트 루트 밖의 파일을 import 하지 못하므로, dev/build 직전에
   이 스크립트가 자동 실행됩니다 (package.json 의 predev / prebuild).

   ui/src/generated/ 는 .gitignore 대상입니다. 절대 직접 편집하지 마세요.
   상태머신을 고칠 때는 protocol/state_machine_v2.json 만 고칩니다.           */
import { copyFileSync, mkdirSync, readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const src = join(here, '..', '..', 'protocol', 'state_machine_v2.json')
const outDir = join(here, '..', 'src', 'generated')
const out = join(outDir, 'state_machine_v2.json')

try {
  const spec = JSON.parse(readFileSync(src, 'utf8'))
  if (!spec.nodes || !spec.events || !spec.initial) {
    console.error('❌ state_machine_v2.json 형식 오류 (nodes/events/initial 필요)')
    process.exit(1)
  }
  mkdirSync(outDir, { recursive: true })
  copyFileSync(src, out)
  console.log(`✅ 상태머신 동기화  v${spec.version}  노드 ${Object.keys(spec.nodes).length}개`)
} catch (e) {
  console.error(`❌ ${src} 를 읽지 못했습니다:`, e.message)
  console.error('   ui/ 는 저장소 안에서 실행해야 합니다 (protocol/ 이 상위에 있어야 함).')
  process.exit(1)
}
