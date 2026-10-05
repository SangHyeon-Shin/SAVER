/* 브라우저 없이 UI가 실제로 렌더링되고 터치 흐름이 도는지 확인합니다.
   화면을 띄울 수 없는 환경(원격 PC, CI 등)에서 코드 수정 후 검증용.

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

const HTML = readFileSync(FILE, 'utf8')
// speed: 탐색 시연 배속(시험 시간 단축). 화면 흐름은 같다
const openDom = (qs) => new JSDOM(HTML, {
  runScripts: 'dangerously', pretendToBeVisual: true,
  url: 'file:///SAVER_UI.html' + qs, virtualConsole: vc,
})
const dom = openDom('?speed=6')
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

  check('02 바퀴 내리기 단계 없음 (바퀴는 평소 내려가 있음, 10/5)', !has('바퀴를 내리')())
  check('03 이동 화면 자동 전이', await until(has('환자에게 이동')))

  tap(B('이동 완료'))
  check('04 시작 대기', await until(has('시작 대기')))
  // 한 화면에 지시 하나(D-42): 헤드셋이 먼저(D-43) → 119 [시작]. 단단한 바닥은 사용 조건 전제라 안내 없음
  const steps = () => d.querySelector('[data-steps]')?.getAttribute('data-steps')
  check('04 ① 헤드셋 착용이 가장 먼저, 다른 지시 없음 (D-43)', has('헤드셋을 착용해 주세요')() && !!d.querySelector('[data-headset]')
    && !has('119에 신고')() && !has('단단한 바닥')() && steps() === '1/2')
  tap(BX('헤드셋을 썼습니다'))
  check('04 ② 119 신고·자동심장충격기 요청, 시작 버튼', await until(has('119에 신고하고')) && has('자동심장충격기를 가져다')()
    && !has('헤드셋')() && !has('단단한 바닥')() && !!BX('시작') && steps() === '2/2')

  tap(BX('시작'))
  check('05 겉옷 제거 안내(인식 없음, D-38)', await until(has('겉옷을 벗겨 주세요'))
    && !has('인식')() && !!B('겉옷을 벗겼습니다'))

  tap(B('겉옷을 벗겼습니다'))
  check('21 겉옷 뒤 AED 패드 부착 확인 (v2.2)', await until(has('지금 패드를 붙이세요')))
  tap(B('패드를 붙였습니다'))
  check('06 몸 위치 맞추기 (흉곽 정렬) ① 팔과 다리 먼저, 지시 하나', await until(has('몸 위치 맞추기'))
    && has('팔과 다리를 몸에 붙여')() && !has('초록색 선에')())
  tap(BX('다음'))
  check('06 ② 초록색 선에 맞추기', await until(has('초록색 선에 환자 몸을 맞춰')) && !has('팔과 다리를')())

  tap(B('맞췄습니다'))
  check('07 손가락 센서 (PPG) 착용', await until(has('손가락 센서')))

  tap(B('다음'))
  check('09 압박 위치 찾기 (v2.1: 고정 전에 탐색)', await until(has('압박 위치 찾기')))
  check('09 미연결이면 모의 데이터로 표시', has('모의 데이터')())
  check('09 어린이 선택지 없음 (성인 대상)', !T().includes('어린이') && has('성인')())
  check('09 진입만으로 탐색 시작 안 함', d.querySelectorAll('[data-pt]').length === 0 && has('찾기 시작을 누르면')())

  tap(BX('찾기 시작'))
  check('09 반력 곡선 그려짐', await until(() => d.querySelectorAll('[data-pt]').length >= 10, 6000),
    d.querySelectorAll('[data-pt]').length + '점')
  check('09 가슴 두께(EAPD)·거리 D 표시', has('205.8 mm')() && has('89.7 mm')())
  check('20 탐색 완료 → 안전핀 뽑기 (바퀴는 핀을 뽑으면 저절로 올라감, 지시 하나)', await until(has('안전핀을 뽑으세요'), 9000)
    && !has('바퀴를 올리')() && !has('헤드셋')())
  tap(B('핀을 뽑았습니다'))
  check('20 ② 헤드셋 확인 (D-40)', await until(has('헤드셋을 썼는지')) && !!d.querySelector('[data-headset]'))

  tap(B('확인했습니다'))
  check('08 바닥 고정 확인 (핀 해제 뒤)', await until(has('바닥 고정 확인')))

  tap(B('재확인'))
  check('10 압박 준비: 지시는 손 떼기 하나', await until(has('압박 준비')) && has('환자에게서 손을 떼세요')() && !has('시작하려면')())
  check('10 자동심장충격기 패드 상태 표시', has('자동심장충격기 패드: 붙임')())

  tap(d.querySelector('#root [role="button"]'))
  check('11 압박 수행 진입', await until(has('압박 모니터링')))

  const v1 = () => T().match(/\d+:\d\d/)?.[0]
  const t0 = v1()
  check('11 실시간 수치 갱신', await until(() => v1() && v1() !== t0, 4000), `${t0} → ${v1()}`)
  check('11 압박 깊이/속도 표시', /[\d.]+ cm/.test(T()) && /\d+ \/min/.test(T()),
    T().match(/[\d.]+ cm|\d+ \/min/g)?.join(' '))

  tap(BX('자동심장충격기 사용'))
  check('22 압박 중 AED 사용 → AED 화면 (압박 멈춤)', await until(has('음성 안내를 따르세요')))
  const tm = () => d.querySelector('[data-aed-timer]')?.textContent
  const tm0 = tm()
  await wait(1600)
  check('22 자동 재개 없음, 중단 경과 시간 증가', has('음성 안내를 따르세요')() && tm() !== tm0, `${tm0} → ${tm()}`)
  check('22 경고 기준 미설정이면 경고 없음', has('경고 기준 미설정')() && !has('길어지고 있습니다')())
  check('22 지시는 자동심장충격기 안내 따르기 하나', !has('분석과 충격 중에는')() && !has('지금 붙이세요')())
  tap(BX('압박 재개'))
  check('22 → 11 구조자 재개로만 압박 화면 복귀', await until(has('압박 모니터링')))

  tap(B('일시정지'))
  check('12 일시정지: 지시는 환자 상태 확인 하나', await until(has('압박을 멈췄습니다')) && has('환자 상태를 확인하세요')())
  check('12 일시정지 중에도 AED 사용 버튼', !!BX('자동심장충격기 사용'))

  tap(BX('재개')); await until(has('압박 모니터링'))
  tap(BX('정지'))
  check('16 안전정지', await until(has('안전 정지')))

  tap(B('수동 해제'))
  check('17 수동 해제 ① 바닥 고정 풀기, 지시 하나', await until(has('바닥 고정을 푸세요')) && !has('가슴에서 떼세요')())
  tap(BX('다음'))
  check('17 ② 장치를 가슴에서 떼기', await until(has('가슴에서 떼세요')) && !has('바닥 고정을 푸세요')())

  tap(BX('완료'))
  check('18 종료·기록 저장', await until(has('작동을 마쳤습니다')))

  check('이미지 5장 로드', d.querySelectorAll('img[src^="data:image"]').length >= 0)
  check('런타임 오류 없음', errors.length === 0, errors[0] || '')

  // 실측 표시 규칙: 생략·stale 필드는 '—', 모의값으로 채우지 않는다 (vitalsView.js)
  const { vitalsView, NA } = await import('../src/vitalsView.js')
  const demo = vitalsView(30, null, false)
  check('수치: 미연결은 모의값', !demo.live && / cm$/.test(demo.depth), demo.depth)
  const full = vitalsView(30, { cp_depth_mm: 49.2, cp_rate_cpm: 109.8, cp_force_N: 382.1 }, false)
  check('수치: 연결 시 받은 값 표시', full.live && full.depth === '4.9 cm' && full.rate === '109.8 /min' && full.force === '약 39 kg',
    [full.depth, full.rate, full.force].join(' '))
  const part = vitalsView(30, { cp_depth_mm: 49.2, cp_rate_cpm: 109.8 }, false)
  check('수치: 생략된 압박력은 — (모의값 금지)', part.force === NA, part.force)
  check('수치: 압박 횟수는 지어내지 않음', part.count === NA, part.count)
  const st = vitalsView(30, { cp_depth_mm: 49.2, cp_rate_cpm: 109.8, cp_force_N: 382.1 }, true)
  check('수치: stale 이면 모두 —', st.depth === NA && st.rate === NA && st.force === NA)
  const bad = vitalsView(30, { cp_depth_mm: null, cp_force_N: 'x' }, false)
  check('수치: 숫자가 아니면 —', bad.depth === NA && bad.force === NA)

  // 상태머신 v2.1: S13 redirect 판정 (Jetson state_machine.py 와 같은 경우)
  const { redirectTarget } = await import('../src/redirect.js')
  const spec = JSON.parse(readFileSync('src/generated/state_machine_v2.json', 'utf8'))
  const rule = spec.nodes.S13.redirect.BACK_TO_IDLE
  check('상태머신: 탐색 → 핀 해제 → 고정 순서',
    spec.nodes.X_PPG.on.PPG_ATTACHED === 'X_SEARCH' && spec.nodes.X_SEARCH.on.SEARCH_DONE === 'X_PIN'
    && spec.nodes.X_PIN.on.PIN_RELEASED === 'S5' && spec.nodes.S5.on.VACUUM_OK === 'S6')
  check('redirect: 플래그 미지원(v1.0)이면 검사 안 함', redirectTarget(rule, null) === null)
  check('redirect: rearm_lock 이면 S12', redirectTarget(rule, { rearm_lock: true, vac_released: false }) === 'S12')
  check('redirect: 해제 확인되면 그대로', redirectTarget(rule, { rearm_lock: false, vac_released: true }) === null)
  check('redirect: vac_released 없음 → S12', redirectTarget(rule, { rearm_lock: false }) === 'S12')
  check('redirect: rearm_lock 값 없음(오래됨) → S12', redirectTarget(rule, { rearm_lock: null, vac_released: true }) === 'S12')

  // 압박점 탐색 판정 (jetson/saver/search.py 와 같은 합성 곡선, tests/test_all.py 9절)
  const S = await import('../src/search.js')
  const SD = await import('../src/searchDemo.js')
  const xs = Array.from({ length: 16 }, (_, i) => 5 * (i + 1))
  const sharp = [...Array(8).fill(40), ...Array(8).fill(12)]
  const t1 = S.detectTip(xs, sharp, 3, 0.3)
  check('판정: 급감 → 처음 30 % 감소 점 (Python 과 같음)', t1.tipIndex === 8 && t1.tipX === 45 && Math.abs(t1.r[8] - 0.7) < 1e-9)
  check('판정: 완만한 감소는 판정 안 함', S.detectTip(xs, xs.map((_, i) => 40 - 28 * i / 15), 3, 0.3).tipIndex === null)
  const two = S.detectTip(xs, [...Array(5).fill(40), 12, ...Array(5).fill(40), ...Array(5).fill(12)], 3, 0.3)
  check('판정: 후보 두 묶음 → 첫 후보, 묶음 수 2', two.tipIndex === 5 && two.groups === 2)
  check('판정: 시작 N 점 안 급감은 N 번째 점', S.detectTip(xs.slice(0, 6), [40, 12, 12, 12, 12, 12], 3, 0.3).tipIndex === 3)
  const P7 = { cp_dist_ref_mm: 89.7, eapd_ref_mm: 205.8, cp_dist_max_mm: 95 }
  check('D: 식 (7), 상한, EAPD 없으면 null',
    Math.abs(S.cpDistance(205.8, P7) - 89.7) < 1e-9 && S.cpDistance(250, P7) === 95
    && [null, 0, -1, NaN, '205.8'].every((e) => S.cpDistance(e, P7) === null))

  const cfg = JSON.parse(readFileSync('../jetson/config.sim.json', 'utf8'))
  check('시연 값 = jetson/config.sim.json', Object.entries(SD.DEMO).every(([k, v]) => cfg[k] === v),
    Object.entries(SD.DEMO).filter(([k, v]) => cfg[k] !== v).map(([k]) => k).join(','))
  const run = (o) => { let last = null; for (const it of SD.demoSearch(o)) last = it.snap; return last }
  const r0 = run({ eapd: 205.8 })
  check('시연: 말단 60 mm 부근, 압박점 = 말단 - D, 도달',
    r0.phase === 'done' && Math.abs(r0.tip_x - 60) <= 5 && Math.abs(r0.target_x - (r0.tip_x - 89.7)) < 0.01
    && Math.abs(r0.pos_x - r0.target_x) <= 0.5 && r0.points.length === 24, `tip=${r0.tip_x} target=${r0.target_x}`)
  const rn = run({ eapd: null })
  check('시연: EAPD 없음 → 동작 없이 거부', rn.reason === 'EAPD_MISSING' && !rn.moved && rn.points.length === 0)
  check('시연: EAPD 250 → D 상한 95', run({ eapd: 250 }).D_mm === 95)
  check('시연: 말단 없음 → TIP_NOT_FOUND', run({ eapd: 205.8, flat: true }).reason === 'TIP_NOT_FOUND')

  const lv = S.searchView({ phase: 'scan', points: [{ x: 5, F: 40 }], rule: { dir: 1, drop_ratio: 0.3, ref_n: 3, roi_mm: 120, step_mm: 5 } })
  check('표시: 연결 시 받은 값만 (모의 아님, 기준 없음)', lv.pts.length === 1 && !lv.simulated && lv.pts[0].ref === null && lv.tipU === null)
  check('표시: 반력 생략·비숫자 점은 그리지 않음', S.searchView({ phase: 'scan', points: [{ x: 5 }, { x: 10, F: 'x' }] }).pts.length === 0)
  const ng = S.searchView({ phase: 'move', points: [{ x: -5, F: 40 }], tip_x: -60, target_x: 29.7, rule: { dir: -1 } })
  check('표시: 방향 부호 -1 이어도 머리 쪽이 왼쪽', ng.tipU === 60 && ng.targetU === -29.7 && ng.pts[0].u === 5)
  check('표시: 브리지가 모의 EAPD 라고 알리면 모의 표시', S.searchView({ phase: 'scan', eapd_mm: 205.8, eapd_sim: true }).eapdSim
    && !S.searchView({ phase: 'scan', eapd_mm: 205.8 }).eapdSim)
  check('표시: 단일 엔코더 모드면 표시, 없으면 표시 안 함', S.searchView({ phase: 'scan', enc_mode: 1 }).encMode === 1
    && S.searchView({ phase: 'scan', enc_mode: 2 }).encMode === 2 && S.searchView({ phase: 'scan' }).encMode === null)
  check('사유: EAPD 없음 안내에 추정값 미사용 명시', S.reasonText('EAPD_MISSING').sub.includes('짐작한 값으로'))

  // 실패 시나리오 (오프라인 시연)
  const d2 = openDom('?start=X_SEARCH&eapd=none').window.document
  const T2 = () => (d2.querySelector('#root')?.textContent || '')
  await until(() => T2().includes('찾기 시작'), 3000)
  tap([...d2.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '찾기 시작'))
  check('EAPD 없음: 안내 표시, 장치 동작·곡선 없음, 화면 유지',
    await until(() => T2().includes('가슴 두께를 재지 못했습니다'), 3000) && T2().includes('측정 안 됨')
    && d2.querySelectorAll('[data-pt]').length === 0 && T2().includes('압박 위치 찾기'))
  const d3 = openDom('?start=X_SEARCH&phantom=flat&speed=8').window.document
  const T3 = () => (d3.querySelector('#root')?.textContent || '')
  await until(() => T3().includes('찾기 시작'), 3000)
  tap([...d3.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '찾기 시작'))
  check('말단 없음: 오류 화면(S10)에 탐색 사유', await until(() => T3().includes('찾지 못했습니다'), 9000)
    && T3().includes('압박 위치 찾기가 중단'))
  tap([...d3.querySelectorAll('#root button')].find((b) => b.textContent.includes('재확인')))
  check('재확인 → 탐색 화면 복귀, 이전 곡선 지움', await until(() => T3().includes('찾기 시작을 누르면'), 3000))
  const d4 = openDom('?start=S13&vac_residual=1').window.document
  const T4 = () => (d4.querySelector('#root')?.textContent || '')
  await until(() => T4().includes('대기로'), 3000)
  tap([...d4.querySelectorAll('#root button')].find((b) => b.textContent.includes('대기로')))
  check('음압 잔존 시연: 종료 → 대기로 누르면 수동 해제(S12)', await until(() => T4().includes('바닥 고정을 푸세요'), 3000))
  tap([...d4.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '다음'))
  await until(() => T4().includes('가슴에서 떼세요'), 3000)
  tap([...d4.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '완료'))
  await until(() => T4().includes('대기로'), 3000)
  tap([...d4.querySelectorAll('#root button')].find((b) => b.textContent.includes('대기로')))
  check('해제 완료 뒤 대기로 → 시작 대기(S2)', await until(() => T4().includes('시작 대기'), 3000))
  // 인공호흡 안내: 시간이 끝나도 압박을 자동으로 다시 시작하지 않는다
  const d5 = openDom('?start=X_VENT').window.document
  const T5 = () => (d5.querySelector('#root')?.textContent || '')
  await until(() => T5().includes('인공호흡 안내'), 3000)
  tap([...d5.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '건너뛰기'))
  await wait(300)
  check('인공호흡: 건너뛰기는 마지막 단계로, 압박 재개 버튼만 남음', T5().includes('인공호흡 안내') && T5().includes('[압박 재개]를 누르세요')
    && !![...d5.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '압박 재개'))
  await wait(2500)
  check('인공호흡: 기다려도 자동으로 압박 화면에 가지 않음', T5().includes('인공호흡 안내') && !T5().includes('압박 모니터링'))
  tap([...d5.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '압박 재개'))
  check('인공호흡: [압박 재개]를 눌러야 압박 화면', await until(() => T5().includes('압박 모니터링'), 3000))
  // 음성 안내 문구 (src/voice/prompts.json)
  const VP = JSON.parse(readFileSync('src/voice/prompts.json', 'utf8'))
  const vids = VP.prompts.map((p) => p.id)
  check('음성 문구: ID 중복 없음, 화면 노드 존재', new Set(vids).size === vids.length
    && VP.prompts.every((p) => p.node === '*' || spec.nodes[p.node]),
    VP.prompts.filter((p) => p.node !== '*' && !spec.nodes[p.node]).map((p) => p.id).join(','))
  check('음성 문구: TTS 입력에 숫자·영문 없음(발음대로 한글)', VP.prompts.every((p) => !/[0-9A-Za-z]/.test(p.say)),
    VP.prompts.filter((p) => /[0-9A-Za-z]/.test(p.say)).map((p) => p.id).join(','))
  const VB = VP.direction.plain.banned
  check('음성 문구: TTS 입력에 어려운 말 없음(일반인 기준, direction.plain)', VB.length >= 10
    && VP.prompts.every((p) => !VB.some((w) => p.say.includes(w))),
    VP.prompts.filter((p) => VB.some((w) => p.say.includes(w))).map((p) => p.id).join(','))
  check('음성 문구: 헤드셋 착용 전 화면은 무음, 첫 음성은 착용 뒤 119 안내 (D-43)',
    ['S1', 'X_WHEELS', 'X_MOVE'].every((n) => VP.prompts.filter((p) => p.node === n).every((p) => p.silent))
    && VP.prompts.filter((p) => p.node === 'S2' && p.when === 'enter').every((p) => p.silent)
    && VP.prompts.some((p) => p.node === 'S2' && p.when === 'step:2' && p.say.includes('일일구') && !p.silent)
    && !VP.prompts.some((p) => p.say.includes('단단한 바닥')))
  check('음성 문구: 바퀴를 내리거나 올리라는 문구 없음 (바퀴는 안전핀으로 저절로, 10/5)',
    VP.prompts.filter((p) => !p.hold).every((p) => !/바퀴를 (내리|올려)/.test(p.say)) && VP.prompts.find((p) => p.id === 'V02').hold === true)
  check('음성 문구: 시험 묶음이 목록에 있고 세 등급을 모두 포함', VP.pilot.every((i) => vids.includes(i))
    && ['안내', '주의', '경고'].every((lv) => VP.pilot.some((i) => VP.prompts.find((p) => p.id === i).level === lv)))
  // 보이스 태그: 등급 태그는 비었거나 [..] 하나, calm 은 등급 태그에 넣지 않고 calm 목록(극도의 침착함)에만
  const TAGOK = (t) => t === '' || /^\[[^\]]+\]$/.test(t)
  const tags = Object.values(VP.direction.levels).map((l) => l.tag).concat(VP.prompts.filter((p) => 'tag' in p).map((p) => p.tag))
  check('음성 문구: 보이스 태그 형식, 긴장 태그 없음, calm 은 목록에만, 목소리 필터와 생성 설정',
    VP.prompts.every((p) => VP.direction.levels[p.level]) && tags.every(TAGOK) && tags.every((t) => !/serious|urgent|calm/i.test(t))
    && VP.direction.calm.ids.every((i) => vids.includes(i)) && VP.direction.levels['안내'].tag === ''
    && VP.direction.filters.length >= 4 && VP.direction.settings.length >= 6)
  check('음성 문구: S0 외 모든 화면에 문구', Object.keys(spec.nodes).filter((n) => n !== 'S0')
    .every((n) => VP.prompts.some((p) => p.node === n)),
    Object.keys(spec.nodes).filter((n) => n !== 'S0' && !VP.prompts.some((p) => p.node === n)).join(','))
  // 화면 문구: 일반인 구조자가 보는 화면에 전문용어를 쓰지 않는다 (AED 는 '자동심장충격기(AED)' 병기만 허용)
  const JARGON = ['자발순환', 'ROSC', '흡착', '튜빙', '유도선', '정렬', 'PPG', 'EAPD', '흉곽', '흉부', '반력', '탐색', '압박점',
    '음압', '밀착', '말단', 'CPR', '엔코더', '로드셀', '펌웨어', '프로토콜']
  const jhits = []
  for (const n of Object.keys(spec.nodes).filter((k) => k !== 'S0')) {
    const dn = openDom('?start=' + n)
    // 글자 덩어리(텍스트 노드)마다 끊어 본다. 이어 붙이면 '찾음'+'압박'이 '음압'으로 잡힌다
    const tx = () => {
      const r = dn.window.document.querySelector('#root'); if (!r) return ''
      const w = dn.window.document.createTreeWalker(r, 4); const out = []
      while (w.nextNode()) out.push(w.currentNode.nodeValue)
      return out.join('\n')
    }
    await until(() => tx().length > 0, 3000, 50)
    const hit = tx().trim() ? JARGON.filter((w) => tx().includes(w)) : ['(빈 화면)']
    const aed = tx().replace(/자동심장충격기\(AED\)|AED in use/g, '').includes('AED')
    if (hit.length || aed) jhits.push(n + ':' + hit.concat(aed ? ['AED'] : []).join('/'))
    dn.window.close()
  }
  check('화면 문구: 전문용어 없음(일반인 기준)', jhits.length === 0, jhits.join(' '))
  // 음성 재생 규칙과 재생기 (voice/plan.js, voice/player.js)
  const VPl = await import('../src/voice/plan.js')
  const VPP = VP.prompts
  check('재생 규칙: 헤드셋 착용 전(부팅, 이동, 시작 대기 진입)은 무음, 착용 뒤 2단계에서 119 안내', ['S1', 'X_WHEELS', 'X_MOVE', 'S2'].every((n) => VPl.planEnter(VPP, n).length === 0)
    && VPl.planEvent(VPP, 'S2', 'step:2').join() === 'V36', VPl.planEnter(VPP, 'S2').join())
  check('재생 규칙: 단계 화면 (몸 위치, 안전핀, 수동 해제)', VPl.planEnter(VPP, 'S3S4').join() === 'V08' && VPl.planEvent(VPP, 'S3S4', 'step:2').join() === 'V07'
    && VPl.planEnter(VPP, 'X_PIN').join() === 'V14' && VPl.planEvent(VPP, 'X_PIN', 'step:2').join() === 'V41'
    && VPl.planEnter(VPP, 'S12').join() === 'V27' && VPl.planEvent(VPP, 'S12', 'step:2').join() === 'V42'
    && VPl.planEnter(VPP, 'X_WHEELS').length === 0)
  check('재생 규칙: 의학 검토 전 보류 문구(V19)는 재생 계획에 없음', VP.prompts.find((p) => p.id === 'V19').hold === true && !VPl.planEnter(VPP, 'S9').includes('V19'))
  check('재생 규칙: S10 은 진입 문구 뒤 원인 문구, 위치 오류는 V09',
    VPl.planEnter(VPP, 'S10', { cause: 'vacuum' }).join() === 'V20,V22' && VPl.planEnter(VPP, 'S3S4', { positionError: true }).join() === 'V09'
    && VPl.planEnter(VPP, 'S3S4').join() === 'V08')
  check('재생 규칙: 압박 중 사건 음성 없음, AED 화면은 경고(V39)만, 배터리는 어느 화면에서나',
    VPl.planEvent(VPP, 'S7', 'battery_low').length === 0 && VPl.planEvent(VPP, 'X_AED', 'battery_low').length === 0
    && VPl.planEvent(VPP, 'X_AED', 'aed_pause_warn').join() === 'V39' && VPl.planEvent(VPP, 'S3S4', 'battery_low').join() === 'V35'
    && VPl.planEvent(VPP, 'X_VENT', 'step:3').join() === 'V31' && VPl.planEvent(VPP, 'X_SEARCH', 'search_fail:EAPD_MISSING').join() === 'V13')
  const { createVoice } = await import('../src/voice/player.js')
  const fake = [], objs = []   // 가짜 Audio: play 하면 기록. V20 은 파일 없음(오류)
  const mk = (src) => { const a = { src, onended: null, onerror: null, paused: false,
    play() { fake.push(src); if (src.includes('V20')) setTimeout(() => a.onerror && a.onerror(), 0); return Promise.resolve() },
    pause() { a.paused = true } }; objs.push(a); return a }
  const pv = createVoice({ prompts: VPP, makeAudio: mk })
  pv.enter('S10', { cause: 'vacuum' })
  check('재생기: 한 번에 하나만 재생', fake.join() === 'voice/V20.mp3', fake.join())
  await wait(10)
  check('재생기: 없는 파일은 건너뛰고 다음', fake.join() === 'voice/V20.mp3,voice/V22.mp3', fake.join())
  fake.length = 0
  pv.enter('S2')
  check('재생기: 시작 대기 진입(헤드셋 착용 전)은 무음', fake.length === 0, fake.join())
  pv.cue('step:2')
  check('재생기: [헤드셋을 썼습니다] 뒤 119 안내가 첫 음성', fake.join() === 'voice/V36.mp3', fake.join())
  pv.enter('S3S4'); const o08 = objs[objs.length - 1]
  o08.onended(); await wait(10)
  check('재생기: 단계 문구는 구조자가 다음을 누르기 전에는 재생 안 함', fake.slice(-1).join() === 'voice/V08.mp3', fake.join())
  pv.enter('S3S4'); const o08b = objs[objs.length - 1]
  pv.cue('step:2')
  check('재생기: 단계를 넘기면 앞 문구를 끊고 그 단계 문구만', o08b.paused
    && fake.slice(-1).join() === 'voice/V07.mp3' && pv.lastIds.join() === 'V07', fake.join())
  pv.enter('S6')
  check('재생기: 화면이 바뀌면 끊고 새 문구', objs[objs.length - 2].paused && fake[fake.length - 1] === 'voice/V16.mp3')
  pv.setMuted(true); pv.enter('S2'); pv.event('battery_low')
  check('재생기: 소리 끄면 재생 안 함', fake[fake.length - 1] === 'voice/V16.mp3')
  pv.setMuted(false); pv.enter('X_AED'); pv.event('battery_low')
  check('재생기: AED 화면은 진입 1회 뒤 무음, 다시 듣기는 마지막 묶음',
    fake[fake.length - 1] === 'voice/V38.mp3' && (pv.replay(), fake[fake.length - 1] === 'voice/V38.mp3') && pv.lastIds.join() === 'V38')
  const pv2 = createVoice({ prompts: VPP, makeAudio: () => { throw new Error('x') } })
  pv2.enter('S10', { cause: 'vacuum' })
  check('재생기: 장치 오류는 건너뛰고 멈추지 않음', pv2.played.join() === 'V20,V22')
  const pv3 = createVoice({ prompts: VPP, makeAudio: mk })
  pv3.enter('S3S4'); pv3.event('battery_low'); pv3.event('battery_low')
  check('재생기: 같은 경고가 겹치면 한 번만', pv3.played.length === 1 && (objs[objs.length - 1].onended(), objs[objs.length - 1].onended(), objs[objs.length - 1].onended(), pv3.played.filter((i) => i === 'V35').length === 1), pv3.played.join())
  const vb = d.querySelector('#root [data-voice]')
  check('화면: 헤더에 다시 듣기와 소리 끄기 버튼', !!vb && vb.textContent.includes('다시 듣기') && vb.querySelectorAll('button').length === 2)
  check('런타임 오류 없음 (시나리오 포함)', errors.length === 0, errors[0] || '')

  console.log(`\n${fail === 0 ? '✅ 전체 통과' : '❌ 실패 있음'}  (통과 ${pass} / 실패 ${fail})\n`)
  dom.window.close()
  process.exit(fail === 0 ? 0 : 1)
})()
