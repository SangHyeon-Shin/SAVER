import React, { useState, useEffect, useCallback, useRef } from 'react'
import { C, SCREEN_W, SCREEN_H } from './tokens'
import { EV, NODES, INITIAL, transition } from './stateMachine'
import { useWaves, useVitals } from './vitals'
import { useBridge } from './bridge'
import { useSearchDemo, DEMO } from './searchDemo'
import { Screen09 } from './screens/Search'
import { Btn } from './ui'
import * as A from './screens/SectionA'
import * as B from './screens/SectionB'
import * as Cs from './screens/SectionC'
import * as DE from './screens/SectionDE'

/* 노드 → 화면 컴포넌트 */
const SCREEN = {
  S1: A.Screen01, X_WHEELS: A.Screen02, X_MOVE: A.Screen03, S2: A.Screen04,
  X_CLOTHES: A.Screen05, S3S4: A.Screen06, X_PPG: A.Screen07, S5: A.Screen08,
  X_SEARCH: Screen09, X_PIN: A.Screen20, S6: A.Screen10,
  S7: B.Screen11, S8: B.Screen12,
  S9: Cs.Screen13, S10: Cs.Screen14, X_POSOUT: Cs.Screen15, S11: Cs.Screen16,
  S12: DE.Screen17, S13: DE.Screen18, X_VENT: DE.Screen19,
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
   브리지에 연결되면 쓰지 않는다. 체험판·스모크 시험용 */
const QS = new URLSearchParams(typeof location === 'undefined' ? ''
  : (location.search || location.hash.replace(/^#/, '')))      // 체험판은 #start=... 로도 받는다
const qEapd = () => {
  const e = QS.get('eapd')
  if (e === 'none') return null
  const n = parseFloat(e)
  return Number.isFinite(n) ? n : DEMO.sim_eapd_mm
}

export default function App() {
  const [ctx, setCtx] = useState(() => (NODES[QS.get('start')] ? { ...INITIAL, node: QS.get('start') } : INITIAL))
  const [demo, setDemo] = useState({ positionError: false, vacuumError: false, clothesStuck: false,
                                     searchEapd: qEapd(), searchFlat: QS.get('phantom') === 'flat' })
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
        log: [...prev.log.slice(-199),
              { ts: Date.now(), from: prev.node, event: snap.event || 'SYNC',
                to: snap.node, state: snap.state }],
      }
    })
  }, [])

  const bridge = useBridge({ onState: applyState })

  /* ---------- 이벤트 발생 (모든 화면의 공통 진입점) ----------
     연결 시  : intent 만 보내고 화면은 브리지가 돌려준 state 로 바뀐다
     미연결 시: 로컬 전이 (오프라인 시연 전용)                        */
  const fire = useCallback((event, payload = {}) => {
    if (bridgeRef.current && bridgeRef.current(event, payload)) return
    setCtx((prev) => {
      const { next, changed } = transition(prev, event, payload)
      if (!changed) { console.debug('[무시된 이벤트]', prev.node, event); return prev }
      return next
    })
  }, [])

  bridgeRef.current = bridge.connected ? bridge.sendIntent : null

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
  const search = bridge.connected ? bridge.search : sdemo.snap
  const searchCtl = bridge.connected
    ? { live: true, start: () => bridge.sendSearch('start'), stop: () => bridge.sendSearch('stop') }
    : { live: false, start: sdemo.start, stop: () => sdemo.stop('USER_STOP') }
  const prevNode = useRef(ctx.node)
  useEffect(() => {
    const was = prevNode.current
    prevNode.current = ctx.node
    if (ctx.node === 'X_SEARCH' && was !== 'X_SEARCH') { sdemo.reset(); bridge.clearSearch() }
    if (ctx.node !== 'X_SEARCH' && was === 'X_SEARCH') sdemo.stop('LEFT_NODE')
  }, [ctx.node])
  const setPatient = useCallback((patient) => setCtx((c) => ({ ...c, patient })), [])

  const vitals = useVitals(!!node.running, bridge.telemetry, bridge.stale)

  /* ---------- 자동 전이 (부팅 · 바퀴 전개) ----------
     연결 시에는 브리지의 ticker 가 처리하므로 UI 는 관여하지 않는다.
     양쪽이 동시에 쏘면 같은 전이가 두 번 기록된다. */
  useEffect(() => {
    if (!node.auto || bridge.connected) return
    const t = setTimeout(() => fire(node.auto.event), node.auto.ms)
    return () => clearTimeout(t)
  }, [node, fire, bridge.connected])

  /* ---------- 압박 재시작 시 카운터 초기화 ---------- */
  useEffect(() => { if (ctx.node === 'S2') vitals.reset() }, [ctx.node])

  /* ---------- 개발/시연 패널 열기: 좌상단 1.5초 길게 누르기 or D키 ---------- */
  useEffect(() => {
    const onKey = (e) => { if (e.key === 'd' || e.key === 'D') setDevOpen((v) => !v) }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [])
  const holdRef = useRef(null)
  const holdStart = () => { holdRef.current = setTimeout(() => setDevOpen((v) => !v), 1500) }
  const holdEnd = () => clearTimeout(holdRef.current)

  const Screen = SCREEN[ctx.node]
  const props = { fire, ctx, demo, vitals, waves, search, searchCtl, setPatient }

  return (
    <div style={{ position: 'fixed', inset: 0, background: '#0a0d12', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
      <div style={{
        width: SCREEN_W, height: SCREEN_H, position: 'relative', overflow: 'hidden',
        transform: `scale(${scale})`, transformOrigin: 'center center', flexShrink: 0,
        boxShadow: '0 0 0 1px rgba(255,255,255,.06)',
      }}>
        {ctx.node === 'S0'
          ? <PowerOff onPress={() => fire(EV.POWER_ON)} />
          : <Screen {...props} />}

        {/* 장치가 아직 준비되지 않아 전이가 거부됨 */}
        {bridge.rejected && bridge.rejected.reason === 'device_not_ready' && (
          <div style={{
            position: 'absolute', left: '50%', bottom: 92, transform: 'translateX(-50%)',
            background: 'rgba(232,168,0,.95)', color: '#1a1f29', borderRadius: 8,
            padding: '12px 22px', fontSize: 20, fontWeight: 700, zIndex: 45,
            boxShadow: '0 6px 24px rgba(0,0,0,.4)', pointerEvents: 'none',
          }}>
            장치 준비 중입니다 · 잠시 후 다시 눌러 주세요
          </div>
        )}

        {/* 배터리 부족 (화면 20) */}
        {ctx.battery && <DE.BatteryBadge />}
        {ctx.battery === 'popup' && <DE.BatteryPopup onClose={() => setCtx((c) => ({ ...c, battery: true }))} />}

        {/* 개발/시연 패널 열기용 히든 핫스팟 (좌상단 90×60px 길게 누르기) */}
        <div
          onPointerDown={holdStart} onPointerUp={holdEnd} onPointerLeave={holdEnd}
          style={{ position: 'absolute', left: 0, top: 0, width: 90, height: 60, zIndex: 50 }}
        />
      </div>

      {devOpen && (
        <DevPanel
          ctx={ctx} node={node} demo={demo} setDemo={setDemo} setCtx={setCtx}
          fire={fire} bridge={bridge} scale={scale} onClose={() => setDevOpen(false)}
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
function DevPanel({ ctx, node, demo, setDemo, setCtx, fire, bridge, scale, onClose }) {
  const box = { position: 'fixed', right: 0, top: 0, bottom: 0, width: 300, background: '#11151b', color: '#dfe4ea', padding: 14, overflowY: 'auto', fontSize: 13, fontFamily: 'ui-monospace,Menlo,monospace', zIndex: 100, borderLeft: '1px solid #232b37' }
  const h = { fontSize: 11, color: '#7c8492', marginTop: 14, marginBottom: 6, letterSpacing: '.08em' }
  const btn = (on) => ({ padding: '7px 9px', borderRadius: 6, border: `1px solid ${on ? C.warning : '#39424f'}`, background: on ? 'rgba(242,96,0,.18)' : '#1b212b', color: on ? C.warning : '#c3cad4', fontSize: 12, fontFamily: 'inherit', textAlign: 'left' })
  const toggle = (k) => setDemo((d) => ({ ...d, [k]: !d[k] }))

  const events = [
    ['ROSC 의심', EV.ROSC_SUSPECT], ['기기 오류', EV.ERROR], ['위치 이탈', EV.POSITION_DRIFT],
    ['인공호흡 가이드', EV.VENT_GUIDE], ['일시정지', EV.PAUSE], ['정지(안전정지)', EV.STOP],
  ]

  return (
    <div style={box}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <b style={{ fontSize: 14 }}>SAVER 시연 패널</b>
        <button onPointerDown={onClose} style={{ ...btn(false), padding: '4px 8px' }}>닫기</button>
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
        </div>
        <div style={{ color: bridge.linkUp ? C.success : '#7c8492' }}>
          ● Teensy {bridge.linkUp ? '연결됨' : '미연결'}
          {bridge.stale && <span style={{ color: C.warning }}> · stale</span>}
        </div>
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
        <button style={btn(demo.clothesStuck)} onPointerDown={() => toggle('clothesStuck')}>겉옷 인식 실패 (화면05)</button>
        <button style={btn(!!ctx.battery)} onPointerDown={() => setCtx((c) => ({ ...c, battery: c.battery ? false : 'popup' }))}>배터리 부족 (화면20)</button>
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
