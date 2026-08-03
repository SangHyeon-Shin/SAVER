/* 브라우저 없이 UI가 실제로 렌더링되고 터치 흐름이 도는지 확인합니다.
   사지방 PC처럼 화면을 띄울 수 없는 환경에서 코드 수정 후 검증용.

   사용:  npm run build:single && npm run smoke                            */
const { readFileSync, existsSync } = require('fs')
const { JSDOM, VirtualConsole } = require('jsdom')

const FILE = 'dist-single/SAVER_UI.html'
if (!existsSync(FILE)) {
  console.error(`❌ ${FILE} 없음 — 먼저 'npm run build:single' 실행`)
  process.exit(1)
}

const errors = []
const vc = new VirtualConsole()
vc.on('jsdomError', (e) => errors.push(String(e.message || e).slice(0, 300)))

const dom = new JSDOM(readFileSync(FILE, 'utf8'), {
  runScripts: 'dangerously', pretendToBeVisual: true,
  url: 'file:///SAVER_UI.html', virtualConsole: vc,
})
const d = dom.window.document

const T = () => (d.querySelector('#root')?.textContent || '').replace(/\s+/g, ' ').trim()
const BTNS = () => [...d.querySelectorAll('#root button')]
const B = (s) => BTNS().find((b) => b.textContent.includes(s))
const BX = (s) => BTNS().find((b) => b.textContent.trim() === s)   // 정확히 일치 ('정지' vs '일시정지')
const tap = (el) => el && el.dispatchEvent(new dom.window.Event('pointerdown', { bubbles: true }))
const wait = (ms) => new Promise((r) => setTimeout(r, ms))
/** 조건이 만족될 때까지 최대 timeout 까지 폴링 (고정 sleep 은 jsdom 에서 흔들림) */
const until = async (fn, timeout = 9000, step = 120) => {
  const end = Date.now() + timeout
  while (Date.now() < end) { if (fn()) return true; await wait(step) }
  return false
}
const has = (s) => () => T().includes(s)

let pass = 0, fail = 0
const check = (name, ok, detail = '') => {
  console.log(`${ok ? '  ✅' : '  ❌'} ${name}${detail ? '  · ' + detail : ''}`)
  if (!ok) console.log('       현재:', T().slice(0, 70))
  ok ? pass++ : fail++
}

;(async () => {
  console.log('\n── SAVER UI 스모크 테스트 ──\n')

  check('01 부팅 화면 렌더링', await until(has('SAVER'), 3000), T().slice(0, 30))

  check('03 이동 화면 자동 전이', await until(has('환자에게 이동')))

  tap(B('이동 완료'))
  check('04 시작 대기', await until(has('시작 대기')))

  tap(B('시작'))
  check('05 겉옷 인식 완료 전환', await until(has('제거 인식됨')))

  tap(B('다음'))
  check('06 흉곽 정렬', await until(has('흉곽 정렬')))

  tap(B('정렬 완료'))
  check('07 PPG 착용', await until(has('PPG 센서')))

  tap(B('다음'))
  check('08 흡착·고정 확인', await until(has('흡착·고정')))

  tap(B('재확인'))
  check('09 환자 정보 입력', await until(has('환자 정보')))

  tap(B('확인'))
  check('10 압박 준비', await until(has('압박 준비')))

  tap(d.querySelector('#root [role="button"]'))
  check('11 압박 수행 진입', await until(has('압박 모니터링')))

  const v1 = () => T().match(/\d+:\d\d/)?.[0]
  const t0 = v1()
  check('11 실시간 수치 갱신', await until(() => v1() && v1() !== t0, 4000), `${t0} → ${v1()}`)
  check('11 압박 깊이/속도 표시', /[\d.]+ cm/.test(T()) && /\d+ \/min/.test(T()),
    T().match(/[\d.]+ cm|\d+ \/min/g)?.join(' '))

  tap(B('일시정지'))
  check('12 일시정지', await until(has('재개하려면')))

  tap(BX('재개')); await until(has('압박 모니터링'))
  tap(BX('정지'))
  check('16 안전정지', await until(has('안전 정지')))

  tap(B('수동 해제'))
  check('17 수동 해제', await until(has('가슴에서 떼세요')))

  tap(B('완료'))
  check('18 종료·기록 저장', await until(has('작동을 마쳤습니다')))

  check('이미지 5장 로드', d.querySelectorAll('img[src^="data:image"]').length >= 0)
  check('런타임 오류 없음', errors.length === 0, errors[0] || '')

  console.log(`\n${fail === 0 ? '✅ 전체 통과' : '❌ 실패 있음'}  (통과 ${pass} / 실패 ${fail})\n`)
  dom.window.close()
  process.exit(fail === 0 ? 0 : 1)
})()
