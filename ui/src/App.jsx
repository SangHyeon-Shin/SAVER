import React, { useState, useEffect, useCallback, useRef } from 'react'
import { C, SCREEN_W, SCREEN_H } from './tokens'
import { EV, NODES, INITIAL, transition } from './stateMachine'
import { useWaves, useVitals } from './vitals'
import { useBridge } from './bridge'
import { radarView } from './radarView'
import { useSearchDemo, DEMO } from './searchDemo'
import { Screen09 } from './screens/Search'
import { Screen21, Screen22 } from './screens/Aed'
import { Btn } from './ui'
import * as U from './screens/Units'
import { UNIT_IDS } from './units'
import { useSimDevice } from './unitSim'
import VOICE from './voice/prompts.json'
import { createVoice } from './voice/player'
import { VoiceCtx } from './voice/VoiceContext'
import * as A from './screens/SectionA'
import * as B from './screens/SectionB'
import * as Cs from './screens/SectionC'
import * as DE from './screens/SectionDE'

/* 노드 → 화면 컴포넌트 */
const SCREEN = {
  S1: A.Screen01, X_MOVE: A.Screen03, S2: A.Screen04,
  X_CLOTHES: A.Screen05, S3S4: A.Screen06, X_PPG: A.Screen07, S5: A.Screen08,
  X_SEARCH: Screen09, X_PIN: A.Screen20, S6: A.Screen10,
  S7: B.Screen11, S8: B.Screen12,
  S9: Cs.Screen13, S10: Cs.Screen14, X_POSOUT: Cs.Screen15, S11: Cs.Screen16,
  S12: DE.Screen17, S13: DE.Screen18, X_VENT: DE.Screen19,
  X_AED_PAD: Screen21, X_AED: Screen22,
}

/* ---------- 1024×600 스테이지를 화면에 꽉 차게 스케일 ---------- */
function useStageScale() {
  const [s, setS] = useState(1)
  useEffect(() => {
    const fit = () => setS(Math.min(window.innerWidth / SCREEN_W, window.innerHeight / SCREEN_H))
    fit()
    window.addEventListener('resize', fit)
    window.addEventListener('orientationchange', fit)
    return () => { window.removeEventListener('resize', fit); window.removeEventListener('orientationchange', fit) }
  }, [])
  return s
}

/* 오프라인 시연 옵션 (주소 뒤 ?start=X_SEARCH&eapd=none&phantom=flat&speed=2)
   브리지에 연결되면 쓰지 않는다. 체험판·스모크 시험용.
   ?start=노드 는 시연 메뉴를 건너뛰고 전체 UI 의 그 화면으로, ?unit=compress 는 그 유닛 화면으로 바로 간다 */
const QS = new URLSearchParams(typeof location === 'undefined' ? ''
  : (location.search || location.hash.replace(/^#/, '')         // 체험판은 #start=... 로도 받는다
     || (typeof window !== 'undefined' && window.__SAVER_QS) || ''))   // 한 파일 체험판(srcdoc)은 감싼 페이지가 넣어 준다
// 체험판 빌드(VITE_DEMO=1)에서만: 감싼 페이지와 상태·이벤트를 주고받는다. 실기 빌드에는 들어가지 않는다
const DEMO_BUILD = import.meta.env.VITE_DEMO === '1'
// 체험판에서 ?menu=1 이나 ?unit= 으로 열면 결선 시연 메뉴(유닛 화면)를 보인다. 그 밖의 체험판은 전체 UI 흐름만
const DEMO_MENU = DEMO_BUILD && (QS.has('menu') || UNIT_IDS.includes(QS.get('unit')))
// 실기 키오스크 빌드(VITE_KIOSK=1, npm run build:kiosk): 브리지에 연결되기 전이나 연결이 끊긴 동안
// 화면이 스스로 넘어가지 않고 연결 대기 화면을 보인다(D-44). 로컬 전이는 오프라인 시연 전용이다
const KIOSK = import.meta.env.VITE_KIOSK === '1'
// 오프라인 시연에서 장치 대신 바닥 고정 완료(VACUUM_OK)를 내기까지의 시간. 시연용 값이며 실기 값이 아니다
const DEMO_VACUUM_MS = 3000
const qEapd = () => {
  const e = QS.get('eapd')
  if (e === 'none') return null
  const n = parseFloat(e)
  return Number.isFinite(n) ? n : DEMO.sim_eapd_mm
}

export default function App() {
  const [ctx, setCtx] = useState(() => ({
    ...INITIAL,
    node: NODES[QS.get('start')] ? QS.get('start') : INITIAL.node,
    battery: QS.get('battery') === '1' ? 'popup' : false,
  }))
  const [demo, setDemo] = useState({
    positionError: QS.get('pos_err') === '1', vacuumError: QS.get('vac_err') === '1',
    vacResidual: QS.get('vac_residual') === '1',          // 종료 때 음압이 남은 경우(S13 → S12)
    searchEapd: qEapd(), searchFlat: QS.get('phantom') === 'flat',
    forceSim: QS.get('sim') === '1',                     // 유닛 화면을 모의값으로(장치 명령 안 보냄). 개발 패널에서도 켠다
  })
  const demoRef = useRef(demo); demoRef.current = demo
  /* 화면 묶음 (결선 시연 메뉴, D-51): 'menu' | 'full'(S1~S13 흐름) | 유닛 4개.
     운용 브리지(상태머신 흐름)에 연결되면 메뉴 없이 'full'. 체험판(감싼 페이지)도 'full' */
  const [view, setView] = useState(() => (QS.get('start') || (DEMO_BUILD && !DEMO_MENU)) ? 'full'
    : UNIT_IDS.includes(QS.get('unit')) ? QS.get('unit') : 'menu')
  const [devOpen, setDevOpen] = useState(() => new URLSearchParams(location.search).has('dev'))
  const scale = useStageScale()
  const waves = useWaves()

  const node = NODES[ctx.node]
  const bridgeRef = useRef(null)

  /* ---------- 브리지가 확정한 상태를 화면에 반영 ---------- */
  const applyState = useCallback((snap) => {
    setCtx((prev) => {
      if (prev.node === snap.node) return prev
      return {
        ...prev,
        node: snap.node,
        errorCause: snap.error_cause || null,
        errorWhy: snap.error_why || null,       // 오류 화면의 세부 사유(예: 초기화 점검이 멈춘 단계와 장치 거부 사유)
        log: [...prev.log.slice(-199),
              { ts: Date.now(), from: prev.node, event: snap.event || 'SYNC',
                to: snap.node, state: snap.state }],
      }
    })
  }, [])

  const liveRef = useRef(false)
  // 브리지는 연결 직후 현재 상태(SYNC)를 먼저 보낸다. 그때는 아직 live 가 정해지기 전이라 버리면, 브리지가 이미
  // S1 을 지난 경우 화면이 S1 에 남는다(10/23 젯슨 "시스템 점검 중" 멈춤). 마지막 상태를 두었다가 live 가 되면 그린다
  const lastSnapRef = useRef(null)
  const bridge = useBridge({ onState: (snap) => { lastSnapRef.current = snap; if (liveRef.current) applyState(snap) } })
  /* 브리지 모드: 'operate'(상태머신 흐름, 브리지가 주인) | 'unit_demo'(결선 시연 모드). 설정을 받기 전은 null.
     시연 모드에서 전체 UI 는 모형(로컬 전이)이고, 유닛 화면만 장치와 주고받는다 */
  const mode = bridge.config ? (bridge.config.mode || 'operate') : null
  const unitDemo = mode === 'unit_demo'
  const live = bridge.connected && !unitDemo           // 전체 UI 가 브리지 상태를 그리는 운용 흐름
  liveRef.current = live
  useEffect(() => { if (!bridge.connected) lastSnapRef.current = null }, [bridge.connected])   // 다시 연결되면 새 SYNC 를 기다린다
  useEffect(() => { if (live && lastSnapRef.current) applyState(lastSnapRef.current) }, [live])
  const unitDemoRef = useRef(false); unitDemoRef.current = unitDemo
  const [everUp, setEverUp] = useState(false)          // 키오스크: 한 번이라도 연결됐는지(끊김 안내 구분)
  useEffect(() => { if (bridge.connected) setEverUp(true) }, [bridge.connected])
  const linkWait = KIOSK && !unitDemo && !bridge.connected
  useEffect(() => { if (mode === 'operate' && view !== 'full') setView('full') }, [mode])

  /* ---------- 이벤트 발생 (모든 화면의 공통 진입점) ----------
     연결 시  : intent 만 보내고 화면은 브리지가 돌려준 state 로 바뀐다
     미연결 시: 로컬 전이 (오프라인 시연 전용)                        */
  const fire = useCallback((event, payload = {}) => {
    // AED 패드 부착 여부는 화면 표시용으로 UI 가 기억한다(장치 상태가 아님)
    if (event === EV.AED_PADS_ON || event === EV.AED_NONE) setCtx((c) => ({ ...c, aedPads: event === EV.AED_PADS_ON }))
    if (bridgeRef.current && bridgeRef.current(event, payload)) return
    if (KIOSK && !unitDemoRef.current) return   // 실기 운용: 연결 없이는 전이하지 않는다(시연 모드의 전체 UI 는 모형)
    // 오프라인 시연: 음압 잔존이면 장치 플래그를 붙여 S13 → S12 redirect 를 보인다(redirect.js)
    if (event === EV.BACK_TO_IDLE && demoRef.current.vacResidual) {
      payload = { ...payload, mcuFlags: { rearm_lock: true, vac_released: false } }
    }
    if (event === EV.RELEASE_DONE && demoRef.current.vacResidual) setDemo((d) => ({ ...d, vacResidual: false }))
    setCtx((prev) => {
      const { next, changed } = transition(prev, event, payload)
      if (!changed) { console.debug('[무시된 이벤트]', prev.node, event); return prev }
      return next
    })
  }, [])

  bridgeRef.current = live ? bridge.sendIntent : null

  /* ---------- 압박점 탐색 ----------
     연결 시  : 시작·중지 요청만 보내고 브리지의 search 메시지를 그린다.
                SEARCH_DONE·SEARCH_FAIL 은 브리지가 정한다.
     미연결 시: 모의 시연(searchDemo.js). 끝나면 로컬 전이                   */
  const sdemo = useSearchDemo({
    eapd: demo.searchEapd, flat: demo.searchFlat, speed: parseFloat(QS.get('speed')) || 1,
    onDone: (ok, snap) => {
      if (ok) fire(EV.SEARCH_DONE)
      else if (snap && snap.moved && !['USER_STOP', 'LEFT_NODE'].includes(snap.reason)) fire(EV.SEARCH_FAIL, { cause: 'search' })
    },
  })
  /* ---------- 음성 안내 (voice/plan.js 규칙, voice/<ID>.mp3) ----------
     화면이 바뀌면 그 화면 문구를 말한다. 파일이 없거나 재생이 막히면 조용히 건너뛴다 */
  const voiceRef = useRef(null)
  if (!voiceRef.current) voiceRef.current = createVoice({ prompts: VOICE.prompts, log: (...a) => console.debug('[음성]', ...a) })
  const voice = voiceRef.current
  const [muted, setMuted] = useState(false)
  const toggleMute = useCallback(() => { const m = !voice.muted; voice.setMuted(m); setMuted(m) }, [voice])
  const voiceValue = React.useMemo(() => ({ voice, muted, toggleMute }), [voice, muted, toggleMute])
  useEffect(() => {
    if (view !== 'full') return
    voice.enter(ctx.node, { cause: ctx.errorCause, positionError: demoRef.current.positionError })
  }, [ctx.node, view])
  useEffect(() => { if (ctx.battery === 'popup') voice.event('battery_low') }, [ctx.battery])
  useEffect(() => () => voice.stop(), [])
  useEffect(() => { if (linkWait) voice.stop() }, [linkWait])

  const search = live ? bridge.search : sdemo.snap
  const startSearch = live ? () => bridge.sendSearch('start') : sdemo.start
  const searchCtl = {
    live,
    start: () => { voice.event('search_start'); startSearch() },   // 장치가 움직이기 전에 손 떼기 안내
    stop: live ? () => bridge.sendSearch('stop') : () => sdemo.stop('USER_STOP'),
  }
  const searchReason = search && search.reason
  useEffect(() => {
    if (ctx.node === 'X_SEARCH' && searchReason) voice.event('search_fail:' + searchReason)
  }, [searchReason])
  const prevNode = useRef(ctx.node)
  useEffect(() => {
    const was = prevNode.current
    prevNode.current = ctx.node
    if (ctx.node === 'X_SEARCH' && was !== 'X_SEARCH') { sdemo.reset(); bridge.clearSearch() }
    if (ctx.node !== 'X_SEARCH' && was === 'X_SEARCH') sdemo.stop('LEFT_NODE')
  }, [ctx.node])
  const setPatient = useCallback((patient) => setCtx((c) => ({ ...c, patient })), [])

  /* ---------- 체험판 연동 (VITE_DEMO=1 빌드, iframe 안에서만) ----------
     감싼 페이지 → {type:'saver-demo', fire, payload, demo, battery}
     이 화면  → {type:'saver-state', node, screen, name, allowed, errorCause}
     브리지에 연결돼 있으면 받지 않는다. */
  useEffect(() => {
    if (!DEMO_BUILD || window.parent === window) return undefined
    const onMsg = (e) => {
      const m = e.data
      if (e.source !== window.parent || !m || m.type !== 'saver-demo' || bridgeRef.current) return
      if (m.demo) setDemo((d) => ({ ...d, ...m.demo }))
      if (m.battery) setCtx((c) => ({ ...c, battery: 'popup' }))
      if (m.fire) fire(m.fire, m.payload || {})
    }
    window.addEventListener('message', onMsg)
    return () => window.removeEventListener('message', onMsg)
  }, [fire])
  useEffect(() => {
    if (!DEMO_BUILD || window.parent === window) return
    const n = NODES[ctx.node]
    window.parent.postMessage({ type: 'saver-state', node: ctx.node, screen: n.screen, name: n.name, view,
      allowed: view === 'full' ? Object.keys(n.on || {}) : [], errorCause: ctx.errorCause,
      demo: { ...demo }, search: search ? { phase: search.phase, reason: search.reason } : null }, '*')
  }, [ctx.node, ctx.errorCause, demo, search && search.phase, view])

  const vitals = useVitals(!!node.running && view === 'full', live ? bridge.telemetry : null, live ? bridge.stale : false)

  /* ---------- 자동 전이 (오프라인 시연 전용, offline_auto) ----------
     브리지에 연결되면 S1 은 브리지의 초기화 절차(장치 응답)로 넘어가므로 UI 는 관여하지 않는다(v2.4).
     양쪽이 동시에 쏘면 같은 전이가 두 번 기록된다. */
  const localFlow = !live && !(KIOSK && !unitDemo) && view === 'full'   // 로컬 전이로 도는 전체 UI(오프라인 시연, 모형)
  useEffect(() => {
    if (!node.auto || !localFlow) return
    const t = setTimeout(() => fire(node.auto.event), node.auto.ms)
    return () => clearTimeout(t)
  }, [node, fire, localFlow])

  /* ---------- 오프라인 시연: 바닥 고정 완료를 장치 대신 알린다 (10/14) ----------
     실기에서는 장치가 음압에 도달하면 VACUUM_OK 를 보내고 브리지가 S5 → S6 으로 넘긴다(jetson bridge.py _on_evt).
     오프라인 시연에는 장치가 없어 [재확인]을 눌러야만 넘어갔으므로 같은 이벤트를 DEMO_VACUUM_MS 뒤에 낸다.
     흡착 오류 시연(vac_err)은 주의 화면을 보여야 하므로 [재확인]으로 넘긴다. 실기 키오스크에서는 하지 않는다 */
  useEffect(() => {
    if (ctx.node !== 'S5' || !localFlow || demo.vacuumError) return undefined
    const t = setTimeout(() => fire(EV.VACUUM_OK), DEMO_VACUUM_MS)
    return () => clearTimeout(t)
  }, [ctx.node, localFlow, demo.vacuumError, fire])

  /* ---------- 압박 재시작 시 카운터 초기화 ---------- */
  useEffect(() => {
    if (ctx.node === 'S2') { vitals.reset(); setCtx((c) => (c.aedPads === undefined ? c : { ...c, aedPads: undefined })) }
  }, [ctx.node])

  /* ---------- 개발/시연 패널 열기: 좌상단 1.5초 길게 누르기 or D키 ---------- */
  useEffect(() => {
    const onKey = (e) => { if (e.key === 'd' || e.key === 'D') setDevOpen((v) => !v) }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [])
  const holdRef = useRef(null)
  const holdFired = useRef(false)
  const holdStart = () => {
    holdFired.current = false
    holdRef.current = setTimeout(() => { holdFired.current = true; setDevOpen((v) => !v) }, 1500)
  }
  const holdEnd = () => clearTimeout(holdRef.current)

  const Screen = SCREEN[ctx.node]
  // AED 압박 중단 경고 기준(초). 의학 근거 확인 전이라 기본은 비어 있다(브리지 CONFIG.aed_pause_warn_s)
  const aedWarnS = bridge.connected && bridge.config ? bridge.config.aed_pause_warn_s : null
  // 일시정지 확인(v2.3): 장치에 연결돼 있으면 Teensy 가 CPR_PAUSED 를 보고한 뒤에만 '멈췄습니다'와 재개 버튼을 보인다.
  // 오프라인 시연(장치 없음)은 바로 멈춘 것으로 본다.
  // ?paused=0 은 오프라인에서 '멈추는 중' 화면을 보는 시험용(스모크)이다.
  const paused = live
    ? (!!bridge.telemetry && !bridge.stale && bridge.telemetry.state === 'CPR_PAUSED')
    : QS.get('paused') !== '0'
  // MR60BHA2 레이더: 브리지가 보낸 값만 쓴다. 오프라인 시연에는 없다(화면에 칸을 만들지 않는다)
  const radar = live ? bridge.radar : null
  // S1 화면의 장치 연결 안내(운용 흐름에서만). 오프라인 시연, 결선 시연 모드의 전체 UI 는 모형이라 null
  const link = live ? bridge.linkUp : null
  const fw = live ? bridge.fwOk : null           // 장치 펌웨어 판(v1.1) 확인 결과. false 면 S1·S10 에 안내
  const props = { fire, ctx, demo, vitals, waves, search, searchCtl, setPatient, aedWarnS, paused, radar, link, fw }

  /* ---------- 결선 시연 메뉴와 유닛 화면 (D-51) ----------
     결선 시연 모드 브리지에 연결되면 유닛 화면은 장치 값만 그리고 시작·정지 요청만 보낸다.
     연결이 없거나 개발 패널에서 모의를 강제하면 모의값(unitSim.js)으로, 화면에 '모의 데이터'로 표시한다 */
  const unitLive = bridge.connected && unitDemo && !demo.forceSim
  const onUnit = UNIT_IDS.includes(view)
  const sim = useSimDevice({ on: onUnit && !unitLive, cfg: bridge.config, speed: parseFloat(QS.get('speed')) || 1 })
  const udemo = useSearchDemo({ eapd: demo.searchEapd, flat: demo.searchFlat, speed: parseFloat(QS.get('speed')) || 1, onDone: () => {} })
  const unitLiveRef = useRef(false)
  useEffect(() => {
    // 유닛 화면에 들어가고 나갈 때 브리지에 알린다. 나가면(메뉴, 전체 UI, 모의 전환) 진행 중인 장치 동작을 브리지가 멈춘다
    if (unitLive) bridge.sendUnit(onUnit ? view : null, onUnit ? 'enter' : 'leave')
    else if (unitLiveRef.current && bridge.connected) bridge.sendUnit(null, 'leave')
    unitLiveRef.current = unitLive
  }, [view, unitLive])
  const canMenu = !live && (!DEMO_BUILD || DEMO_MENU)
  const goMenu = () => {
    voice.stop()
    sdemo.stop('LEFT_NODE')
    udemo.stop('LEFT_NODE')
    sim.reset()
    setView('menu')
  }
  const pick = (id) => {
    if (id === 'full') {
      // 전체 UI 는 매번 처음(S1)부터 보인다
      sdemo.reset()
      vitals.reset()
      setCtx((c) => ({ ...INITIAL, patient: c.patient }))
    } else if (id === 'drive') {
      udemo.reset()
      bridge.clearSearch()
    }
    sim.reset()
    setView(id)
  }
  const uTel = unitLive ? bridge.telemetry : sim.tel
  const uStale = unitLive ? bridge.stale : false
  const uRadar = unitLive ? bridge.radar : sim.radar
  const simOn = { compress: sim.st.cpr, vacuum: sim.st.vac }
  const uMsg = (u) => (unitLive ? bridge.unit
    : u === 'stiff' ? { unit: u, phase: sim.stiff.phase, stiff: sim.stiff.res }
    : { unit: u, phase: simOn[u] ? 'running' : 'idle' })
  const uAct = (u, key) => ({
    onStart: () => (unitLive ? bridge.sendUnit(u, 'start') : sim.set(key, true)),
    onStop: () => (unitLive ? bridge.sendUnit(u, 'stop') : sim.set(key, false)),
  })
  // 구동: 압박 위치 찾기 화면. 탐색 전에 브리지가 멈추면(장치 미연결 등) 그 사유를 탐색 실패로 보인다
  const du = unitLive && bridge.unit && bridge.unit.unit === 'drive' ? bridge.unit : null
  const driveSearch = !unitLive ? udemo.snap
    : du && du.phase === 'failed' && !bridge.search ? { phase: 'failed', reason: du.reason } : bridge.search
  const driveCtl = {
    live: unitLive,
    linked: bridge.connected,
    start: () => { if (unitLive) { bridge.clearSearch(); bridge.sendUnit('drive', 'start') } else udemo.start() },
    stop: () => (unitLive ? bridge.sendUnit('drive', 'stop') : udemo.stop('USER_STOP')),
  }
  const menuStatus = { sim: !unitLive, bridge: bridge.connected, teensy: bridge.linkUp, fw: bridge.fwOk,
                       radar: radarView(unitLive ? bridge.radar : null).live }

  let body
  if (linkWait) body = <A.ScreenLink lost={everUp} />
  else if (view === 'menu') body = <U.UnitMenu onPick={pick} status={menuStatus} />
  else if (view === 'patient') {
    body = <U.PatientUnit live={unitLive} linked={bridge.connected} tel={uTel} stale={uStale} radar={uRadar} waves={waves} onMenu={goMenu}
      sim={unitLive ? null : sim} />
  } else if (view === 'compress') {
    body = <U.CompressUnit live={unitLive} linked={bridge.connected} tel={uTel} stale={uStale} cfg={bridge.config} msg={uMsg('compress')} waves={waves}
      onMenu={goMenu} {...uAct('compress', 'cpr')} />
  } else if (view === 'stiff') {
    body = <U.StiffUnit live={unitLive} linked={bridge.connected} tel={uTel} stale={uStale} cfg={bridge.config} msg={uMsg('stiff')}
      onMenu={goMenu} {...uAct('stiff', 'stiff')} />
  } else if (view === 'vacuum') {
    body = <U.VacuumUnit live={unitLive} linked={bridge.connected} tel={uTel} stale={uStale} cfg={bridge.config} msg={uMsg('vacuum')}
      onMenu={goMenu} {...uAct('vacuum', 'vac')} />
  } else if (view === 'drive') {
    body = <Screen09 fire={(ev) => { if (ev === EV.CANCEL) goMenu() }} ctx={ctx} search={driveSearch} searchCtl={driveCtl}
      setPatient={setPatient} radar={unitLive ? bridge.radar : null} backLabel="메뉴" again />
  } else if (ctx.node === 'S0') body = <PowerOff onPress={() => fire(EV.POWER_ON)} />
  else body = <Screen {...props} />

  return (
    <div style={{ position: 'fixed', inset: 0, background: '#0a0d12', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
      <div style={{
        width: SCREEN_W, height: SCREEN_H, position: 'relative', overflow: 'hidden',
        transform: `scale(${scale})`, transformOrigin: 'center center', flexShrink: 0,
        boxShadow: '0 0 0 1px rgba(255,255,255,.06)',
      }}>
        <VoiceCtx.Provider value={view === 'full' ? voiceValue : null}>
          {body}
        </VoiceCtx.Provider>

        {/* 전체 UI 모형(로컬 전이): 왼쪽 위를 짧게 누르면 시연 메뉴로 */}
        {view === 'full' && canMenu && !linkWait && (
          <div data-mock-chip style={{ position: 'absolute', left: 4, top: 3, zIndex: 49, pointerEvents: 'none',
            fontSize: 12, fontWeight: 700, color: '#fff', background: 'rgba(20,24,30,.55)', borderRadius: 999,
            padding: '1px 9px', lineHeight: '17px' }}>≡ 메뉴 · 모형</div>
        )}

        {/* 장치가 아직 준비되지 않아 전이가 거부됨 */}
        {view === 'full' && bridge.rejected && ['device_not_ready', 'no_pause_cmd', 'pause_unverified'].includes(bridge.rejected.reason) && (
          <div style={{
            position: 'absolute', left: '50%', bottom: 92, transform: 'translateX(-50%)',
            background: 'rgba(232,168,0,.95)', color: '#1a1f29', borderRadius: 8,
            padding: '12px 22px', fontSize: 20, fontWeight: 700, zIndex: 45,
            boxShadow: '0 6px 24px rgba(0,0,0,.4)', pointerEvents: 'none',
          }}>
            {bridge.rejected.reason === 'no_pause_cmd' || bridge.rejected.reason === 'pause_unverified'
              ? '인공호흡 안내는 실기에서 아직 쓰지 않습니다 (압박 정지 확인 전)'
              : bridge.rejected.need && bridge.rejected.need.includes('CPR_PAUSED')
                ? '압박이 멈추기를 기다리는 중입니다'
                : '장치 준비 중입니다 · 잠시 후 다시 눌러 주세요'}
          </div>
        )}

        {/* 배터리 부족 (화면 20) */}
        {view === 'full' && ctx.battery && <DE.BatteryBadge />}
        {view === 'full' && ctx.battery === 'popup' && <DE.BatteryPopup onClose={() => setCtx((c) => ({ ...c, battery: true }))} />}

        {/* 히든 핫스팟 (좌상단 90×60px): 1.5초 길게 누르면 개발/시연 패널, 짧게 누르면 시연 메뉴로(모형·유닛 화면에서만) */}
        <div
          data-menu-hotspot
          onPointerDown={holdStart}
          onPointerUp={() => { holdEnd(); if (!holdFired.current && canMenu && view !== 'menu' && !linkWait) goMenu() }}
          onPointerLeave={holdEnd}
          style={{ position: 'absolute', left: 0, top: 0, width: 90, height: 60, zIndex: 50 }}
        />
      </div>

      {devOpen && (
        <DevPanel
          ctx={ctx} node={node} demo={demo} setDemo={setDemo} setCtx={setCtx}
          fire={fire} bridge={bridge} scale={scale} onClose={() => setDevOpen(false)}
          mode={mode} onMenu={canMenu ? () => { goMenu(); setDevOpen(false) } : null}
        />
      )}
    </div>
  )
}

/* ---------- S0 · 전원 꺼짐 ---------- */
function PowerOff({ onPress }) {
  return (
    <div onPointerDown={onPress} style={{
      width: '100%', height: '100%', background: '#000', color: '#3a4250',
      display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 18, cursor: 'pointer',
    }}>
      <div style={{ fontSize: 40, fontWeight: 800, letterSpacing: '.08em' }}>SAVER</div>
      <div style={{ fontSize: 20 }}>전원이 꺼졌습니다 · 화면을 눌러 켜기</div>
    </div>
  )
}

/* ================= 개발 / 시연 패널 =================
   실제 시연 시에는 닫아두고, 좌상단을 1.5초 길게 눌러 열 수 있습니다. */
function DevPanel({ ctx, node, demo, setDemo, setCtx, fire, bridge, scale, onClose, mode, onMenu }) {
  const box = { position: 'fixed', right: 0, top: 0, bottom: 0, width: 300, background: '#11151b', color: '#dfe4ea', padding: 14, overflowY: 'auto', fontSize: 13, fontFamily: 'ui-monospace,Menlo,monospace', zIndex: 100, borderLeft: '1px solid #232b37' }
  const h = { fontSize: 11, color: '#7c8492', marginTop: 14, marginBottom: 6, letterSpacing: '.08em' }
  const btn = (on) => ({ padding: '7px 9px', borderRadius: 6, border: `1px solid ${on ? C.warning : '#39424f'}`, background: on ? 'rgba(242,96,0,.18)' : '#1b212b', color: on ? C.warning : '#c3cad4', fontSize: 12, fontFamily: 'inherit', textAlign: 'left' })
  const toggle = (k) => setDemo((d) => ({ ...d, [k]: !d[k] }))
  const rv = radarView(bridge.connected ? bridge.radar : null)        // 개발 패널은 압박 중에도 값을 보인다
  const rr = bridge.radar || {}

  const events = [
    ['AED 사용', EV.AED_PAUSE], ['ROSC 의심', EV.ROSC_SUSPECT], ['기기 오류', EV.ERROR], ['위치 이탈', EV.POSITION_DRIFT],
    ['인공호흡 가이드', EV.VENT_GUIDE], ['일시정지', EV.PAUSE], ['정지(안전정지)', EV.STOP],
  ]

  return (
    <div style={box}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <b style={{ fontSize: 14 }}>SAVER 시연 패널</b>
        <span style={{ display: 'flex', gap: 6 }}>
          {onMenu && <button onPointerDown={onMenu} style={{ ...btn(false), padding: '4px 8px' }}>메뉴로</button>}
          <button onPointerDown={onClose} style={{ ...btn(false), padding: '4px 8px' }}>닫기</button>
        </span>
      </div>

      <div style={h}>현재 상태</div>
      <div style={{ background: '#1b212b', borderRadius: 8, padding: 10, lineHeight: 1.7 }}>
        <div>노드 <b style={{ color: '#fff' }}>{node.id}</b></div>
        <div>상태머신 <b style={{ color: C.wave }}>{node.state || '(정의서)'}</b> · {node.ui}</div>
        <div>화면 {String(node.screen).padStart(2, '0')} · {node.name}</div>
        <div>알람 <span style={{ color: { normal: C.success, caution: C.caution, warning: C.warning, danger: C.danger }[node.alarm] }}>{node.alarm}</span></div>
      </div>

      <div style={h}>연결</div>
      <div style={{ background: '#1b212b', borderRadius: 8, padding: 10, lineHeight: 1.6 }}>
        <div style={{ color: bridge.connected ? C.success : C.caution }}>
          ● 브리지 {bridge.connected ? '연결됨' : '미연결 · 로컬 전이'}
          {mode && <span style={{ color: '#9aa3b0' }}> · {mode === 'unit_demo' ? '결선 시연 모드' : '운용 흐름'}</span>}
        </div>
        <div style={{ color: bridge.linkUp ? C.success : '#7c8492' }}>
          ● Teensy {bridge.linkUp ? '연결됨' : '미연결'}
          {bridge.linkUp && bridge.fwOk === false && <span data-fw-mismatch style={{ color: C.warning }}> · 펌웨어 v1.1 아님</span>}
          {bridge.stale && <span style={{ color: C.warning }}> · stale</span>}
        </div>
        <div data-radar-status style={{ color: rv.live ? C.success : '#7c8492' }}>● {rv.status}</div>
        {rv.enabled && (
          <div data-radar-values style={{ color: '#c5ccd6', fontSize: 12, lineHeight: 1.6 }}>
            호흡 {rv.rr} · 심박 {rv.hr}<br />
            거리 {rv.distance !== null ? rv.distance.toFixed(0) + ' mm' : '—'} · 가슴 두께 {rv.eapd !== null ? rv.eapd.toFixed(0) + ' mm' : (rv.floorSet ? '—' : '레이더 높이 미설정')}<br />
            <span style={{ color: '#7c8492' }}>{rr.port} · 프레임 {rr.frames ?? 0} · 버림 {rr.bad ?? 0}</span>
          </div>
        )}
        <div style={{ color: '#7c8492', fontSize: 11, wordBreak: 'break-all' }}>{bridge.url}</div>
        <div style={{ color: '#7c8492', fontSize: 11 }}>배율 {(scale * 100).toFixed(0)}%</div>
        {bridge.lastError && (
          <div style={{ color: C.warning, fontSize: 11, marginTop: 6 }}>
            {bridge.lastError.code} {bridge.lastError.desc || bridge.lastError.why}
          </div>
        )}
      </div>

      <div style={h}>오류 토글 (센서 대체)</div>
      <div style={{ display: 'grid', gap: 6 }}>
        <button style={btn(demo.positionError)} onPointerDown={() => toggle('positionError')}>위치 오류 (화면06)</button>
        <button style={btn(demo.vacuumError)} onPointerDown={() => toggle('vacuumError')}>흡착 오류 (화면08)</button>
        <button style={btn(!!ctx.battery)} onPointerDown={() => setCtx((c) => ({ ...c, battery: c.battery ? false : 'popup' }))}>배터리 부족 (화면20)</button>
        <button style={btn(demo.vacResidual)} onPointerDown={() => toggle('vacResidual')}>음압 잔존 (화면18 대기로 → 17)</button>
        <button style={btn(demo.forceSim)} onPointerDown={() => toggle('forceSim')}>유닛 화면: 모의값으로 시연 (장치 명령 안 보냄)</button>
      </div>

      <div style={h}>탐색 시연 <span style={{ color: '#5b6470' }}>(장치 미연결 전용 · 모의값)</span></div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 5 }}>
        {[['없음', null], ['180', 180], ['205.8', 205.8], ['250', 250]].map(([label, val]) => (
          <button key={label} style={{ ...btn(demo.searchEapd === val), fontSize: 11, textAlign: 'center' }}
            onPointerDown={() => setDemo((d) => ({ ...d, searchEapd: val }))}>EAPD {label}</button>
        ))}
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 5, marginTop: 5 }}>
        <button style={btn(!demo.searchFlat)} onPointerDown={() => setDemo((d) => ({ ...d, searchFlat: false }))}>모사체 정상</button>
        <button style={btn(demo.searchFlat)} onPointerDown={() => setDemo((d) => ({ ...d, searchFlat: true }))}>말단 없음(균일)</button>
      </div>

      <div style={h}>이벤트 강제 발생 <span style={{ color: '#5b6470' }}>(현재 상태 허용분만 활성)</span></div>
      <div style={{ display: 'grid', gap: 6 }}>
        {events.map(([label, ev]) => {
          const ok = !!(node.on && node.on[ev])
          return (
            <button key={ev} disabled={!ok} onPointerDown={() => ok && fire(ev)}
              style={{ ...btn(false), opacity: ok ? 1 : .35, cursor: ok ? 'pointer' : 'default' }}>
              {label} · {ev}
            </button>
          )
        })}
      </div>

      <div style={h}>화면 바로가기</div>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 5 }}>
        {Object.values(NODES).map((n) => (
          <button key={n.id} style={{ ...btn(n.id === node.id), fontSize: 11, padding: '5px 6px' }}
            onPointerDown={() => { if (!bridge.sendForce(n.id)) setCtx((c) => ({ ...c, node: n.id })) }}>
            {String(n.screen).padStart(2, '0')} {n.state || n.id.replace('X_', '')}
          </button>
        ))}
      </div>

      <div style={h}>이벤트 로그 (최근 12건)</div>
      <div style={{ background: '#0f1319', borderRadius: 8, padding: 8, fontSize: 11, lineHeight: 1.55, color: '#9aa3b0' }}>
        {ctx.log.slice(-12).reverse().map((l, i) => (
          <div key={i}>{new Date(l.ts).toLocaleTimeString('ko-KR', { hour12: false })} {l.from}→{l.to} <span style={{ color: C.wave }}>{l.event}</span></div>
        ))}
        {ctx.log.length === 0 && <div>아직 없음</div>}
      </div>

      <div style={{ marginTop: 14 }}>
        <Btn variant="ghostDark" w="100%" h={40} fs={13} onPress={() => setCtx({ ...INITIAL, node: 'S1' })}>처음부터 다시</Btn>
      </div>
    </div>
  )
}
