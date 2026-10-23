/* 브라우저 없이 UI가 실제로 렌더링되고 터치 흐름이 도는지 확인합니다.
   화면을 띄울 수 없는 환경(원격 PC, CI 등)에서 코드 수정 후 검증용.

   사용:  npm run build:single && npm run smoke                            */
const { readFileSync, existsSync, readdirSync } = require('fs')
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

  // 결선 시연 메뉴(D-51): 첫 화면은 메뉴다. [전체 UI]를 눌러야 S1 부터 기존 화면 흐름(모형)
  check('00 첫 화면은 시연 메뉴: 전체 UI, 환자 상태, 압박, 강성 측정, 흡착 (구동은 메뉴에서 뺌, 10/23)',
    await until(() => d.querySelectorAll('[data-unit]').length === 5, 3000)
    && ['전체 UI', '환자 상태', '압박', '강성 측정', '흡착'].every((t) => has(t)()) && !d.querySelector('[data-unit="drive"]'))
  check('00 메뉴: 장치 미연결이면 모의 데이터 표시, 전체 UI 는 모형, 장치는 각 화면의 [시작]으로만',
    has('오프라인 · 모의 데이터')() && has('모형 화면')() && has('[시작]을 눌러야 움직입니다')())
  tap(d.querySelector('[data-unit="full"]'))
  check('01 부팅 화면 렌더링', await until(has('시스템 점검 중'), 3000) && has('SAVER')(), T().slice(0, 30))
  check('01 전체 UI 모형 표시(왼쪽 위 메뉴)', !!d.querySelector('[data-mock-chip]'))

  check('02 바퀴 내리기 단계 없음 (바퀴는 평소 내려가 있음, 10/5)', !has('바퀴를 내리')())
  // 한 화면에 지시 하나(D-42): 장치를 끌고 가기 전에 119 신고가 먼저(10/14 팀장) → 이동 → 헤드셋(D-43)
  const steps = () => d.querySelector('[data-steps]')?.getAttribute('data-steps')
  check('03 ① 이동 전에 119 신고, 자동심장충격기 요청은 주변에 사람이 있을 때 (10/14)', await until(has('119에 신고하세요'), 6000)
    && has('주변에 사람이 있으면 자동심장충격기를')() && !has('헤드셋')() && !has('이동 완료')() && steps() === '1/2')
  tap(BX('다음'))
  check('03 ② 환자에게 이동', await until(has('환자 위치로 끌고 이동해 주세요')) && !has('119에 신고')() && steps() === '2/2')

  tap(B('이동 완료'))
  check('04 시작 대기', await until(has('시작 대기')))
  check('04 지시는 헤드셋 착용 하나, 119 안내 없음 (D-43, 10/14)', has('헤드셋을 착용해 주세요')() && !!d.querySelector('[data-headset]')
    && !has('119')() && !has('단단한 바닥')() && !BX('시작'))
  tap(BX('헤드셋을 썼습니다'))
  check('05 외투만 벗기기 안내(인식 없음, D-38, 10/14 외투만)', await until(has('외투만 벗겨 주세요'))
    && !has('인식')() && !has('겉옷')() && !has('맨살')() && !!B('외투를 벗겼습니다'))

  tap(B('외투를 벗겼습니다'))
  check('21 외투 뒤 AED 패드 부착 확인 (v2.2)', await until(has('지금 패드를 붙이세요')))
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

  check('10 압박은 [압박 시작] 버튼으로만 시작 (10/14 결정 ①, 화면 전체 터치 없음)', !!BX('압박 시작')
    && !has('화면을 누르면')() && !d.querySelector('#root [role="button"]'))
  tap(BX('압박 시작'))
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
  check('17 수동 해제 ① 바닥 고정이 풀릴 때까지 기다리기(장치가 음압을 끔), 지시 하나', await until(has('바닥 고정이 풀릴 때까지 기다리세요')) && !has('환자에게서 치우세요')())
  tap(BX('다음'))
  check('17 ② 장치를 환자에게서 치우기', await until(has('환자에게서 치우세요')) && !has('풀릴 때까지 기다리세요')())

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

  // 레이더(MR60BHA2) 표시 규칙 (radarView.js, D-50): 받은 값만, 압박이 멈춘 뒤에만 호흡·심박
  const { radarView } = await import('../src/radarView.js')
  const RV = { enabled: true, connected: true, receiving: true, rr_bpm: 14.2, hr_bpm: 71.6, distance_mm: 294, eapd_mm: 206.3, floor_set: true }
  const rOn = radarView(RV, { hold: true })
  check('레이더: 멈춘 뒤 호흡·심박 표시', rOn.live && rOn.rr === '14 회/분' && rOn.hr === '72 회/분' && rOn.eapd === 206.3,
    [rOn.rr, rOn.hr, rOn.eapd].join(' '))
  const rRun = radarView(RV, { hold: false })
  check('레이더: 압박 중(멈춤 보고 전)에는 호흡·심박 —, 가슴 두께는 유지', rRun.rr === NA && rRun.hr === NA && rRun.eapd === 206.3
    && rRun.status === '압박 중에는 표시하지 않음')
  const rMiss = radarView({ enabled: true, connected: true, receiving: true, floor_set: false }, { hold: true })
  check('레이더: 생략된 값은 —, 높이 미설정 표시', rMiss.rr === NA && rMiss.hr === NA && rMiss.eapd === null && !rMiss.floorSet)
  const rQuiet = radarView({ ...RV, receiving: false }, { hold: true })
  check('레이더: 수신이 끊기면 값을 쓰지 않음', !rQuiet.live && rQuiet.rr === NA && rQuiet.eapd === null && rQuiet.status === '레이더 수신 없음')
  check('레이더: 0, 문자, 음수는 —', radarView({ ...RV, rr_bpm: 0, hr_bpm: 'x', eapd_mm: -5 }).rr === NA
    && radarView({ ...RV, hr_bpm: 'x' }).hr === NA && radarView({ ...RV, eapd_mm: -5 }).eapd === null)
  check('레이더: 브리지 미연결·레이더 없음은 칸을 만들지 않음', !radarView(null).enabled && !radarView({ enabled: false }).enabled
    && radarView({ enabled: false }).status === '레이더 사용 안 함')

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
  // D-54(팀장 10/23): 비상정지 버튼 미연결. 초기화 오류 화면에 늘 보이던 비상정지 안내를 빼고 젯슨이 알린 실제 사유를 보인다
  const { initReason } = await import('../src/initReason.js')
  const ir = (w) => initReason(w) || { t: '', s: '' }
  check('초기화 오류 사유(D-54): HOME·ARM 거부는 장치 거부 문구와 원래 코드, 비상정지는 사유가 ESTOP 일 때만',
    ir('HOME_REJECTED:HOME_UNAVAILABLE_NO_VERIFIED_LIMIT').t.includes('원점 복귀')
    && ir('HOME_REJECTED:HOME_UNAVAILABLE_NO_VERIFIED_LIMIT').s.includes('HOME_UNAVAILABLE_NO_VERIFIED_LIMIT')
    && ir('ARM_REJECTED:VACUUM_RELEASE_NOT_VERIFIED').t.includes('구동 준비')
    && ir('ESTOP').t.includes('비상정지') && !ir('UNEXPECTED_MCU_STATE:ESTOP').t.includes('비상정지')
    && initReason(null) === null && initReason('FW_VERSION:1.0') === null)
  check('초기화 오류 화면(D-54): 늘 보이던 "비상정지 버튼을 확인하세요" 안내 없음', !HTML.includes('비상정지 버튼을 확인하세요'))
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
    Math.abs(S.cp_distance(205.8, P7) - 89.7) < 1e-9 && S.cp_distance(250, P7) === 95
    && [null, 0, -1, NaN, '205.8'].every((e) => S.cp_distance(e, P7) === null))

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
  check('사유: 펌웨어 판 불일치(FW_VERSION)와 주행 기능 없음(UNSUPPORTED)을 구분',
    S.reasonText('FW_VERSION').sub.includes('v1.1') && !S.reasonText('UNSUPPORTED').sub.includes('v1.1'))

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
  check('음압 잔존 시연: 종료 → 대기로 누르면 수동 해제(S12)', await until(() => T4().includes('바닥 고정이 풀릴 때까지 기다리세요'), 3000))
  tap([...d4.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '다음'))
  await until(() => T4().includes('환자에게서 치우세요'), 3000)
  tap([...d4.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '완료'))
  await until(() => T4().includes('대기로'), 3000)
  tap([...d4.querySelectorAll('#root button')].find((b) => b.textContent.includes('대기로')))
  check('해제 완료 뒤 대기로 → 시스템 점검(S1, v2.5)', await until(() => T4().includes('시스템 점검 중'), 3000))
  check('대기로 뒤 점검을 마치면 119 신고와 이동 화면(오프라인 시연 3 s)', await until(() => T4().includes('119에 신고하세요'), 6000))
  // 오프라인 시연: 바닥 고정 확인(S5)은 장치의 고정 완료 신호를 흉내 내 3초 뒤 압박 준비로 (10/14)
  const d5v = openDom('?start=S5').window.document
  const T5v = () => (d5v.querySelector('#root')?.textContent || '')
  await until(() => T5v().includes('바닥 고정 확인'), 3000)
  check('08 오프라인 시연: 바닥 고정 확인은 3초 뒤 압박 준비로 자동 (장치 고정 완료 흉내, 10/14)', await until(() => T5v().includes('압박 준비'), 6000))
  const d5e = openDom('?start=S5&vac_err=1').window.document
  const T5e = () => (d5e.querySelector('#root')?.textContent || '')
  await until(() => T5e().includes('바닥 고정 확인'), 3000)
  await wait(4000)
  check('08 흡착 오류 시연은 자동으로 넘어가지 않음 ([재확인]으로 오류 화면)', T5e().includes('고정 약함') && !T5e().includes('압박 준비'))

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
  // v2.3: 장치가 멈춤(CPR_PAUSED)을 보고하기 전에는 안내와 재개를 막는다 (?paused=0 은 오프라인 시험용)
  const pw = async (q, words) => {
    const dd = openDom(q).window.document
    const TT = () => (dd.querySelector('#root')?.textContent || '')
    await until(() => words.every((w) => TT().includes(w)), 3000)
    return { dd, TT }
  }
  const v0 = await pw('?start=X_VENT&paused=0', ['인공호흡 안내', '잠시 기다리세요'])
  check('v2.3 인공호흡: 멈춤 확인 전에는 기다리기만, 단계 안내와 버튼 없음', v0.TT().includes('압박을 멈추는 중입니다')
    && !v0.TT().includes('머리를 젖히고')
    && ![...v0.dd.querySelectorAll('#root button')].some((b) => ['건너뛰기', '압박 재개'].includes(b.textContent.trim())))
  const s8 = await pw('?start=S8&paused=0', ['압박을 멈추는 중입니다'])
  const s8btn = [...s8.dd.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '재개')
  check('v2.3 일시정지: 멈춤 확인 전 재개 버튼 비활성', !!s8btn && s8btn.disabled && !s8.TT().includes('환자 상태를 확인하세요'))
  tap(s8btn); await wait(300)
  check('v2.3 일시정지: 비활성 재개를 눌러도 압박 화면으로 가지 않음', s8.TT().includes('일시정지') && !s8.TT().includes('압박 모니터링'))
  const s9 = await pw('?start=S9&paused=0', ['압박을 멈추는 중입니다'])
  const s9btn = [...s9.dd.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '압박 재개')
  check('v2.3 회복 확인: 멈춤 확인 전 환자 확인 지시 없음, 재개 비활성', !!s9btn && s9btn.disabled && !s9.TT().includes('숨쉬기와 맥박'))
  const ae = await pw('?start=X_AED&paused=0', ['압박 멈추는 중'])
  const aebtn = [...ae.dd.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '압박 재개')
  check('v2.3 자동심장충격기: 멈춤 확인 전 재개 비활성', !!aebtn && aebtn.disabled)
  const s8ok = await pw('?start=S8', ['압박을 멈췄습니다'])
  check('v2.3 일시정지: 오프라인 시연은 바로 멈춘 것으로 보고 재개 가능',
    !![...s8ok.dd.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '재개' && !b.disabled))
  const po = await pw('?start=X_POSOUT', ['압박 위치가 어긋났습니다'])
  check('v2.3 위치 이탈: 다시 맞추기 없이 [정지] 하나', po.TT().includes('[정지]를 누르세요') && !po.TT().includes('맞췄습니다')
    && !po.TT().includes('다시 맞춰') && !![...po.dd.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '정지')
    && ![...po.dd.querySelectorAll('#root button')].some((b) => b.textContent.includes('재개')))
  // v2.4 (한건민 합의 10/5): 인공호흡 안내의 [정지]는 장치가 멈췄다고 알리기 전에도 누를 수 있다
  const vs = await pw('?start=X_VENT&paused=0', ['잠시 기다리세요'])
  const vsb = [...vs.dd.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '정지')
  check('v2.4 인공호흡: 멈춤 확인 전에도 [정지]를 누를 수 있음', !!vsb && !vsb.disabled)
  tap(vsb)
  check('v2.4 인공호흡: [정지] → 안전 정지 화면', await until(() => vs.TT().includes('안전 정지'), 3000), vs.TT().slice(0, 40))
  const vg = await pw('?start=X_VENT', ['인공호흡 안내', '건너뛰기'])
  check('v2.4 인공호흡: 안내 중에도 [정지] 있음',
    !![...vg.dd.querySelectorAll('#root button')].find((b) => b.textContent.trim() === '정지' && !b.disabled))
  // v2.4: X_WHEELS 삭제. 오프라인 시연은 S1 → 119 신고와 이동(offline_auto, 10/14부터 1단계가 119 신고), 실기 브리지는 초기화 절차 결과로
  const s1d = openDom('?start=S1').window.document
  const S1T = () => (s1d.querySelector('#root')?.textContent || '')
  const s1seen = []
  await until(() => { s1seen.push(S1T().includes('점검을 마쳤습니다')); return S1T().includes('119에 신고하세요') }, 6000, 100)
  check('v2.4 부팅: 오프라인 시연은 점검 완료 화면 없이 바로 119 신고와 이동 화면(3 s, offline_auto)',
    S1T().includes('119에 신고하세요') && !s1seen.some(Boolean), S1T().slice(0, 40))
  check('v2.4 상태머신: X_WHEELS 없음, S1 은 초기화 절차(procedure)와 오프라인 전용 자동 전이, 초기화 오류는 S1 로 복귀',
    !spec.nodes.X_WHEELS && !spec.events.WHEELS_DONE && spec.nodes.S1.procedure === 'init' && !spec.nodes.S1.auto
    && spec.nodes.S1.offline_auto && spec.nodes.S1.on.INIT_DONE === 'X_MOVE' && spec.error_return_by_event.INIT_ERROR === 'S1'
    && ['X_MOVE', 'S2', 'X_CLOTHES', 'X_AED_PAD', 'S3S4', 'X_PPG'].every((n) => spec.nodes[n].on.INIT_ERROR === 'S10')
    && spec.nodes.X_VENT.on.STOP === 'S11'
    && /error_return_by_event/.test(readFileSync('src/stateMachine.js', 'utf8')) && /offline_auto/.test(readFileSync('src/stateMachine.js', 'utf8')))
  // 실기 키오스크 빌드(D-44): 브리지에 연결되기 전에는 화면이 스스로 넘어가지 않는다
  const KFILE = 'dist-kiosk/SAVER_UI.html'
  if (!existsSync(KFILE)) check('키오스크: 시험본 있음 (npm run build:single 이 함께 만든다)', false, KFILE)
  else {
    const dk = new JSDOM(readFileSync(KFILE, 'utf8'), { runScripts: 'dangerously', pretendToBeVisual: true,
      url: 'file:///SAVER_UI_kiosk.html', virtualConsole: vc }).window.document
    const TK = () => (dk.querySelector('#root')?.textContent || '').replace(/\s+/g, ' ')
    await until(() => TK().includes('장치와 연결하고 있습니다'), 3000)
    check('키오스크: 연결 전에는 연결 대기 화면', !!dk.querySelector('[data-linkwait]') && TK().includes('장치와 연결하고 있습니다'))
    await wait(4200)
    check('키오스크: 연결 없이 4초가 지나도 스스로 넘어가지 않음 (부팅 점검 3초 타이머 무시)',
      !!dk.querySelector('[data-linkwait]') && !TK().includes('119에 신고하세요') && !TK().includes('점검을 마쳤습니다'), TK().slice(0, 40))
  }
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
  check('음성 문구: 헤드셋 착용 전 화면(119 신고와 이동 포함)은 무음, 첫 음성은 착용 뒤 외투 안내 (D-43, 10/14)',
    ['S1', 'X_MOVE', 'S2'].every((n) => VP.prompts.filter((p) => p.node === n).every((p) => p.silent))
    && VP.prompts.some((p) => p.node === 'X_MOVE' && p.when === 'step:1' && p.say.includes('일일구') && p.say.includes('주변에 사람이 있으면'))
    && VP.prompts.some((p) => p.node === 'X_CLOTHES' && p.when === 'enter' && !p.silent)
    && !VP.prompts.some((p) => p.say.includes('단단한 바닥')))
  check('음성 문구: 바퀴를 내리거나 올리라는 문구 없음 (바퀴는 안전핀으로 저절로, 10/5)',
    VP.prompts.every((p) => !/바퀴를 (내리|올려)/.test(p.say)) && !VP.prompts.some((p) => p.id === 'V02' || p.node === 'X_WHEELS'))
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
  check('음성 문구 v0.11: 마스크 단계와 V30 없음(인공호흡 계획에 마스크 없음, 10/14), V05 는 외투만',
    !vids.includes('V30') && VP.prompts.every((p) => !p.say.includes('마스크') && !p.text.includes('마스크') && !p.say.includes('맨살'))
    && VP.prompts.find((p) => p.id === 'V05').say.includes('외투만') && VP.version === '0.11')
  {
    const need = VP.prompts.filter((p) => !p.silent && !['V35', 'V39'].includes(p.id)).map((p) => p.id)
    const miss = need.filter((i) => !existsSync('public/voice/' + i + '.mp3'))
    const extra = readdirSync('public/voice').filter((f) => f.endsWith('.mp3') && !need.includes(f.slice(0, -4)))
    check('음성 파일: 무음, 제외 문구 말고는 모두 있음(10/14 생성 33개 중 무음이 된 V36 제외 32개), 목록에 없는 파일 없음', need.length === 32 && miss.length === 0 && extra.length === 0,
      'miss ' + miss.join(',') + ' extra ' + extra.join(','))
  }
  // 음성 재생 규칙과 재생기 (voice/plan.js, voice/player.js)
  const VPl = await import('../src/voice/plan.js')
  const VPP = VP.prompts
  check('재생 규칙: 헤드셋 착용 전(부팅, 119 신고와 이동, 시작 대기)은 무음, 첫 음성은 외투 안내(V05)', ['S1', 'X_MOVE', 'S2'].every((n) => VPl.planEnter(VPP, n).length === 0)
    && ['step:1', 'step:2'].every((w) => VPl.planEvent(VPP, 'X_MOVE', w).length === 0) && VPl.planEnter(VPP, 'X_CLOTHES').join() === 'V05', VPl.planEnter(VPP, 'X_CLOTHES').join())
  check('재생 규칙: 단계 화면 (몸 위치, 안전핀, 수동 해제)', VPl.planEnter(VPP, 'S3S4').join() === 'V08' && VPl.planEvent(VPP, 'S3S4', 'step:2').join() === 'V07'
    && VPl.planEnter(VPP, 'X_PIN').join() === 'V14' && VPl.planEvent(VPP, 'X_PIN', 'step:2').join() === 'V41'
    && VPl.planEnter(VPP, 'S12').join() === 'V27' && VPl.planEvent(VPP, 'S12', 'step:2').join() === '')
  check('재생 규칙: V42(바닥 고정이 풀렸습니다)는 해제 완료 판정 전까지 보류 (10/14 결정 ⑤)',
    VP.prompts.find((p) => p.id === 'V42').hold === true && !VPl.planEvent(VPP, 'S12', 'step:2').includes('V42'))
  check('재생 규칙: 의학 검토 전 보류 문구(V19)는 재생 계획에 없음', VP.prompts.find((p) => p.id === 'V19').hold === true && !VPl.planEnter(VPP, 'S9').includes('V19'))
  check('재생 규칙: S10 은 진입 문구 뒤 원인 문구, 위치 오류는 V09',
    VPl.planEnter(VPP, 'S10', { cause: 'vacuum' }).join() === 'V20,V22' && VPl.planEnter(VPP, 'S3S4', { positionError: true }).join() === 'V09'
    && VPl.planEnter(VPP, 'S3S4').join() === 'V08')
  check('재생 규칙: 압박 중 사건 음성 없음, AED 화면은 경고(V39)만, 배터리는 어느 화면에서나',
    VPl.planEvent(VPP, 'S7', 'battery_low').length === 0 && VPl.planEvent(VPP, 'X_AED', 'battery_low').length === 0
    && VPl.planEvent(VPP, 'X_AED', 'aed_pause_warn').join() === 'V39' && VPl.planEvent(VPP, 'S3S4', 'battery_low').join() === 'V35'
    && VPl.planEvent(VPP, 'X_SEARCH', 'search_fail:EAPD_MISSING').join() === 'V13')
  check('재생 규칙: 인공호흡 단계 음성은 마지막 단계 V34만, V29와 V31~V33은 교육용 (10/14 결정 ③)',
    [1, 2, 3, 4].every((k) => VPl.planEvent(VPP, 'X_VENT', 'step:' + k).length === 0) && VPl.planEvent(VPP, 'X_VENT', 'step:5').join() === 'V34'
    && ['V29', 'V31', 'V32', 'V33'].every((i) => VP.prompts.find((p) => p.id === i).edu === true))
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
  pv.enter('X_MOVE'); pv.cue('step:1'); pv.cue('step:2'); pv.enter('S2')
  check('재생기: 119 신고와 이동, 시작 대기(헤드셋 착용 전)는 무음', fake.length === 0, fake.join())
  pv.enter('X_CLOTHES')
  check('재생기: [헤드셋을 썼습니다] 뒤 첫 음성은 외투 안내', fake.join() === 'voice/V05.mp3', fake.join())
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
  // ── 결선 시연 메뉴와 유닛 화면 (D-51) ──
  const UV = await import('../src/units.js')
  const pvLive = UV.patientView({ ppg_hr_bpm: 71.6, spo2_pct: 97.2 }, false, { enabled: true, connected: true, receiving: true, rr_bpm: 14 })
  check('유닛 표시: 환자 상태 실측(호흡은 레이더, 심박·산소포화도는 손가락 센서), 맥박 신호 있음',
    pvLive.rr === '14 회/분' && pvLive.hr === '72 회/분' && pvLive.spo2 === '97 %' && pvLive.pulse && pvLive.rrNum === 14)
  const pvNone = UV.patientView({ state: 'IDLE' }, false, null)
  check('유닛 표시: 손가락 센서 값이 없으면 —, 맥박 신호 없음(0 으로 채우지 않음)',
    pvNone.hr === UV.NA && pvNone.spo2 === UV.NA && !pvNone.pulse && pvNone.rr === UV.NA && pvNone.rrNum === null)
  check('유닛 표시: stale 이면 손가락 센서 값을 쓰지 않음, 0 과 범위 밖 산소포화도는 —',
    UV.patientView({ ppg_hr_bpm: 70, spo2_pct: 97 }, true, null).hr === UV.NA
    && UV.patientView({ ppg_hr_bpm: 0, spo2_pct: 130 }, false, null).spo2 === UV.NA
    && !UV.patientView({ ppg_hr_bpm: 0 }, false, null).pulse)
  const cvL = UV.compressView({ state: 'CPR_RUNNING', cp_depth_mm: 49.24, cp_rate_cpm: 109.6, cp_force_N: 412.3 }, false,
    { target_depth_mm: 50, target_bpm: 110, force_max_N: 600 })
  check('유닛 표시: 압박 실측(깊이 mm, 속도 회/분, 강도 N), 목표값은 브리지 설정에서',
    cvL.running && cvL.depth === '49.2 mm' && cvL.rate === '110 회/분' && cvL.force === '412 N' && cvL.forceKg === '약 42 kgf'
    && cvL.targetDepth === 50 && cvL.forceMax === 600, JSON.stringify(cvL))
  const cvN = UV.compressView({ state: 'HOMED' }, false, { target_depth_mm: null, target_bpm: 110 })
  check('유닛 표시: 압박 값이 없으면 —, 목표 깊이 미설정은 null',
    !cvN.running && cvN.depth === UV.NA && cvN.force === UV.NA && cvN.targetDepth === null && cvN.targetBpm === 110)
  check('유닛 표시: 압박 횟수는 받은 속도를 시간에 대해 더함(값 없음, 2초 넘게 끊긴 구간은 더하지 않음)',
    Math.abs(UV.countStep(0, 120, 1) - 2) < 1e-9 && UV.countStep(5, null, 1) === 5 && UV.countStep(5, 110, 3) === 5)
  const svL = UV.stiffView({ of: 10, done: 10, n: 10, k_N_mm: 10.27, k_sd: 0.07, f_N: 519.7, d_mm: 50.61,
    pts: [[51.3, 523.2], [49.7, 513.3], ['x', 1]] }, { target_depth_mm: 50, target_bpm: 110, force_max_N: 600, model_stiffness_N_mm: 10 })
  check('유닛 표시: 강성 측정 결과(N/mm, 최대 힘, 최대 깊이, 진행, 편차), 비교선은 설정값이 있을 때만',
    svL.complete && svL.kText === '10.3 N/mm' && svL.fText === '520 N' && svL.dText === '50.6 mm' && svL.progress === '10 / 10회'
    && svL.sdText === '± 0.1' && svL.pts.length === 2 && svL.model === 10 && svL.forceMax === 600, JSON.stringify(svL))
  const svN = UV.stiffView({ of: 10, done: 3 }, { stiff_cycles: 10 })
  check('유닛 표시: 강성 측정 값이 없으면 —, 미완료, 비교선 없음',
    !svN.complete && svN.kText === UV.NA && svN.fText === UV.NA && svN.progress === '3 / 10회' && svN.model === null
    && UV.stiffView(null, {}).progress === UV.NA && UV.stiffView(null, { stiff_cycles: 10 }).of === 10)
  const vvL = UV.vacuumView({ state: 'VACUUM_HOLD', vac_kPa: -23.04, vac_leak: false }, false, { vacuum_target_kPa: -22.5 })
  check('유닛 표시: 흡착 음압, 목표, 고정 상태, 누설', vvL.kPaText === '-23.0 kPa' && vvL.targetText === '-22.5 kPa'
    && vvL.holding && vvL.leakText === '없음', JSON.stringify(vvL))
  check('유닛 표시: 목표 음압 미설정, 음압 없음은 —', UV.vacuumView({}, false, {}).targetText === '미설정'
    && UV.vacuumView({}, false, {}).kPaText === UV.NA && UV.vacuumView({ vac_kPa: -20 }, true, {}).kPaText === UV.NA)
  check('유닛 표시: 파형 속도는 받은 분당 횟수에 맞춤, 값이 없으면 정지', UV.waveDur(110, 150) === '4.36s' && UV.waveDur(null, 150) === null)
  check('유닛 사유 문구: 거부 사유를 쉬운 말로, 오류 코드는 그대로',
    UV.unitReasonText('NOT_READY').includes('시연용 펌웨어') && UV.unitReasonText('3-CU-021') === '장치 오류 3-CU-021'
    && UV.unitReasonText('TARGET_NOT_SET').includes('목표 깊이') && UV.unitReasonText('FAULT:3-MU-002').includes('3-MU-002')
    && UV.unitReasonText('FW_VERSION').includes('v1.1'))
  check('유닛 사유 문구(팀장 10/23): 정지 거부는 파워서플라이 전원 차단 안내, 비상정지 버튼 안내 없음',
    UV.unitReasonText('STOP_REJECTED:NOT_CPR_ACTIVE').includes('파워서플라이 전원을 차단')
    && !UV.unitReasonText('STOP_REJECTED:NOT_CPR_ACTIVE').includes('비상정지'))
  check('유닛 상태: 브리지 단계 → 화면 문구, 다른 유닛 상태는 대기',
    UV.phaseView({ unit: 'compress', phase: 'starting', step: 'HOME' }, 'compress').text === '원점 복귀 중'
    && UV.phaseView({ unit: 'vacuum', phase: 'running' }, 'compress').phase === 'idle'
    && UV.phaseView({ unit: 'compress', phase: 'failed', reason: 'NOT_READY' }, 'compress').note.includes('시연용 펌웨어'))

  const dm = openDom('?speed=8').window.document
  const TM = () => (dm.querySelector('#root')?.textContent || '').replace(/\s+/g, ' ')
  const BM = (s) => [...dm.querySelectorAll('#root button')].find((b) => b.textContent.trim() === s)
  const ust = (label) => dm.querySelector(`[data-stat="${label}"]`)?.textContent
  const up = (el) => el && el.dispatchEvent(new dom.window.Event('pointerup', { bubbles: true }))
  const menuBack = () => until(() => dm.querySelectorAll('[data-unit]').length === 5, 2500)
  await menuBack()
  tap(dm.querySelector('[data-unit="patient"]'))
  check('메뉴 → 환자 상태: 호흡·심박수·산소포화도 칸, 모의 데이터 표시, 맥박 신호 대기',
    await until(() => TM().includes('환자 상태'), 3000) && TM().includes('산소포화도') && !!dm.querySelector('[data-source="sim"]')
    && TM().includes('맥박 신호를 기다리고'))
  check('환자 상태: 모의 호흡·손가락을 켜기 전에는 값 없음(—)',
    ust('심박수 / Heart rate') === '—' && ust('산소포화도 / SpO₂') === '—' && ust('호흡수 / Respiration') === '—')
  tap(dm.querySelectorAll('[data-simtoggle]')[1])
  tap(dm.querySelectorAll('[data-simtoggle]')[0])
  check('환자 상태: 값이 오면 심박수·산소포화도·호흡수 표시, 맥박 신호 알림(회복 판정은 사람이)',
    await until(() => / 회\/분/.test(ust('심박수 / Heart rate') || '') && / %/.test(ust('산소포화도 / SpO₂') || '')
      && / 회\/분/.test(ust('호흡수 / Respiration') || ''), 3000) && TM().includes('맥박 신호가 잡힙니다'))
  tap(BM('회복을 확인했습니다'))
  check('환자 상태: [회복을 확인했습니다] → 자발순환 회복 표시(구조자 확인)', await until(() => TM().includes('자발순환 회복(ROSC) · 구조자 확인'), 2000))
  tap(BM('메뉴'))
  check('환자 상태 → [메뉴] → 시연 메뉴', await menuBack())

  tap(dm.querySelector('[data-unit="compress"]'))
  await until(() => TM().includes('압박 리듬'), 3000)
  const hold = dm.querySelector('[data-hold]')
  check('압박: 진입만으로 시작 안 함, 목표값 칸, [길게 눌러 압박 시작]',
    !TM().includes('만지지 마세요') && !!hold && hold.textContent.includes('길게 눌러') && ust('압박 횟수') === '—')
  tap(hold); up(hold)
  await wait(1300)
  check('압박: 짧게 눌렀다 떼면 시작하지 않음(관람객이 스쳐도)', !TM().includes('만지지 마세요'))
  tap(dm.querySelector('[data-hold]'))
  check('압박: 1초 길게 누르면 시작(모의), 깊이·속도·강도 표시',
    await until(() => TM().includes('만지지 마세요') && / mm$/.test(ust('압박 깊이 / Depth') || '')
      && /회\/분$/.test(ust('압박 속도 / Rate') || '') && / N$/.test(ust('압박 강도 / Force') || ''), 4000),
    `${ust('압박 깊이 / Depth')} ${ust('압박 속도 / Rate')} ${ust('압박 강도 / Force')}`)
  check('압박: 경과 시간과 압박 횟수(약 N회)가 늘어남',
    await until(() => /^약 [1-9]\d*회$/.test(ust('압박 횟수') || '') && ust('경과 시간') !== '0:00', 5000),
    `${ust('경과 시간')} ${ust('압박 횟수')}`)
  check('압박: 압박 중에는 시작 버튼 비활성', dm.querySelector('[data-hold]').disabled)
  tap(BM('정지'))
  check('압박: [정지] → 멈춤, 마지막 경과 시간 유지', await until(() => !TM().includes('만지지 마세요'), 3000) && ust('경과 시간') !== '0:00')
  tap(BM('메뉴'))

  await menuBack()
  tap(dm.querySelector('[data-unit="vacuum"]'))
  await until(() => TM().includes('흡착 고정'), 3000)
  check('흡착: 진입만으로 시작 안 함, 해제 비활성', !TM().includes('바닥 고정 완료') && BM('해제').disabled)
  tap(BM('흡착 시작'))
  check('흡착: [흡착 시작] → 음압이 목표 쪽으로, 고정 완료 표시(모의)',
    await until(() => TM().includes('바닥 고정 완료'), 6000) && /^-2\d\.\d kPa$/.test(ust('현재 음압 / Vacuum') || ''), ust('현재 음압 / Vacuum'))
  tap(BM('해제'))
  check('흡착: [해제] → 고정 해제', await until(() => !TM().includes('바닥 고정 완료'), 6000))
  tap(BM('메뉴'))

  await menuBack()
  tap(dm.querySelector('[data-unit="stiff"]'))
  await until(() => TM().includes('힘-깊이'), 3000)
  const sh = dm.querySelector('[data-hold]')
  check('강성 측정: 진입만으로 시작 안 함, [길게 눌러 측정 시작], 결과 칸 —, 점 없음',
    !TM().includes('만지지 마세요') && !!sh && sh.textContent.includes('측정 시작') && ust('유효 강성 / Stiffness') === '—'
    && dm.querySelectorAll('[data-stiff-pt]').length === 0)
  tap(sh); up(sh)
  await wait(1300)
  check('강성 측정: 짧게 눌렀다 떼면 시작하지 않음', !TM().includes('만지지 마세요'))
  tap(dm.querySelector('[data-hold]'))
  check('강성 측정: 1초 길게 누르면 측정(모의), 진행 회수와 힘-깊이 점이 늘어남',
    await until(() => TM().includes('만지지 마세요') || dm.querySelectorAll('[data-stiff-pt]').length > 0, 4000))
  check('강성 측정: 10회 뒤 저절로 멈추고 결과(N/mm), 점 10개와 기울기선, 모의 표시',
    await until(() => TM().includes('측정 완료') && dm.querySelectorAll('[data-stiff-pt]').length === 10
      && !!dm.querySelector('[data-stiff-line]'), 8000)
    && /^\d+\.\d N\/mm$/.test(ust('유효 강성 / Stiffness') || '') && ust('측정 압박') === '10 / 10회'
    && TM().includes('(모의)') && !dm.querySelector('[data-hold]').disabled, `${ust('유효 강성 / Stiffness')} ${ust('측정 압박')}`)
  tap(BM('메뉴'))
  check('강성 측정 → [메뉴] → 시연 메뉴', await menuBack())

  tap(dm.querySelector('[data-unit="full"]'))
  check('전체 UI: 다시 들어가면 처음(부팅 화면)부터, 모형 표시', await until(() => TM().includes('시스템 점검 중'), 3000) && !!dm.querySelector('[data-mock-chip]'))
  await until(() => TM().includes('119에 신고하세요'), 6000)
  const hs = dm.querySelector('[data-menu-hotspot]')
  tap(hs); up(hs)
  check('전체 UI 모형: 왼쪽 위를 짧게 누르면 시연 메뉴로', await menuBack())
  const du = openDom('?unit=vacuum').window.document
  check('바로가기: ?unit=vacuum 은 흡착 화면', await until(() => (du.querySelector('#root')?.textContent || '').includes('흡착 고정'), 3000))
  const dd = openDom('?unit=drive').window.document
  check('개발용 바로가기: ?unit=drive 는 압박 위치 찾기 화면(메뉴에는 없음)',
    await until(() => (dd.querySelector('#root')?.textContent || '').includes('압박 위치 찾기'), 3000))

  const vb = d.querySelector('#root [data-voice]')
  check('화면: 헤더에 다시 듣기와 소리 끄기 버튼', !!vb && vb.textContent.includes('다시 듣기') && vb.querySelectorAll('button').length === 2)
  // 10/13: 운영체제 애니메이션 효과를 끈 PC에서 진행 막대와 파형이 멈춰 장치가 멈춘 것처럼 보였다(index.css 주석)
  check('모션 최소화 설정에서도 진행 막대와 파형이 멈추지 않음 (10/13)', !/prefers-reduced-motion/.test(HTML))
  check('런타임 오류 없음 (시나리오 포함)', errors.length === 0, errors[0] || '')

  console.log(`\n${fail === 0 ? '✅ 전체 통과' : '❌ 실패 있음'}  (통과 ${pass} / 실패 ${fail})\n`)
  dom.window.close()
  process.exit(fail === 0 ? 0 : 1)
})()
