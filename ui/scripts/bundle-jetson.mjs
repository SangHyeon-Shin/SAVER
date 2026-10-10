/* 젯슨 실기용 UI 묶음을 jetson/ui_dist/ 로 복사합니다 (npm run bundle:jetson).

   젯슨 나노(JetPack 4.6, Ubuntu 18.04)는 glibc 2.27 이라 Vite 5 가 요구하는 Node 18 을 돌리지 못합니다.
   그래서 PC 에서 실기용 빌드(build:kiosk, 브리지 연결 전에는 화면이 스스로 넘어가지 않음, D-44)를 만들어
   저장소에 넣고, jetson/setup/setup_jetson.sh 가 이 묶음을 그대로 브리지(--serve)에 물립니다.

   BUILD_INFO.json 에 빌드한 ui/ 와 protocol/ 의 git 트리 해시를 남겨, 설치 스크립트가 묶음이
   최신 소스와 다르면 경고합니다. ui/src 나 protocol/state_machine_v2.json 을 고치면 다시 실행해 커밋합니다. */
import { cpSync, existsSync, rmSync, writeFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { execSync } from 'node:child_process'

const here = dirname(fileURLToPath(import.meta.url))
const ui = join(here, '..')
const repo = join(ui, '..')
const src = join(ui, 'dist')
const out = join(repo, 'jetson', 'ui_dist')

if (!existsSync(join(src, 'index.html'))) {
  console.error('❌ ui/dist/index.html 이 없습니다. npm run build:kiosk 를 먼저 실행하세요.')
  process.exit(1)
}

const git = (cmd) => {
  try { return execSync(`git ${cmd}`, { cwd: repo, stdio: ['ignore', 'pipe', 'ignore'] }).toString().trim() }
  catch { return '' }
}
const dirty = git('status --porcelain -- ui protocol').length > 0

rmSync(out, { recursive: true, force: true })
cpSync(src, out, { recursive: true })
const info = {
  build: 'npm run build:kiosk (VITE_KIOSK=1)',
  commit: git('rev-parse HEAD'),
  ui_tree: git('rev-parse HEAD:ui'),
  protocol_tree: git('rev-parse HEAD:protocol'),
  dirty,
  built_at: new Date().toISOString(),
}
writeFileSync(join(out, 'BUILD_INFO.json'), JSON.stringify(info, null, 2) + '\n')
console.log(`✅ jetson/ui_dist 갱신  commit ${info.commit.slice(0, 7)}${dirty ? '  (커밋 안 된 ui/protocol 변경 포함: 커밋 뒤 다시 실행 권장)' : ''}`)
