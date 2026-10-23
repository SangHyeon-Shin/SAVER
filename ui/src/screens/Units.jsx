import React, { useEffect, useRef, useState } from 'react'
import { C } from '../tokens'
import { Dark, Header, Btn, WavePanel } from '../ui'
import { UNITS, NA, phaseView, patientView, compressView, vacuumView, countStep, waveDur, clock } from '../units'

/* ================= 결선 시연 메뉴와 유닛 화면 (D-51) =================
   운용 상태머신(S1~S13)의 노드가 아니다. 결선 포스터 시연에서 유닛 하나씩 보이는 화면이다.
   - 전체 UI: 기존 S1~S13 화면 흐름을 모형으로(장치 동작 없음) 처음부터 보인다(App.jsx).
   - 환자 상태, 압박, 흡착: 이 파일. 구동: 압박 위치 찾기 화면(screens/Search.jsx)을 그대로 쓴다.
   - 결선 시연 모드 브리지(--unit-demo)에 연결되면 받은 값만 그리고, 시작·정지는 요청만 보낸다.
     연결이 없으면 unitSim.js 의 모의값을 쓰고 '모의 데이터'로 표시한다.
   - 장치는 진입만으로 움직이지 않는다. 압박은 [시작]을 1초 길게 눌러야 요청한다. */

const FLAT = 'M 0 60 L 2400 60'
const BUSY = ['starting', 'running', 'stopping']
const TONE = { idle: '#8b94a3', busy: C.caution, run: C.success, ok: C.success, warn: C.warning }

/* ---------- 메뉴 ---------- */
export function UnitMenu({ onPick, status }) {
  return (
    <Dark>
      <div style={{ height: 84, display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '0 28px',
        borderBottom: '1px solid rgba(255,255,255,.1)', flexShrink: 0 }}>
        <div style={{ display: 'flex', alignItems: 'baseline', gap: 14 }}>
          <div style={{ fontSize: 34, fontWeight: 800, letterSpacing: '.05em' }}>SAVER</div>
          <div style={{ fontSize: 21, color: C.darkSub }}>시연 메뉴</div>
        </div>
        <StatusChips status={status} />
      </div>
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 14, padding: '18px 24px 12px', minHeight: 0 }}>
        <button data-unit="full" onPointerDown={() => onPick('full')} style={tile(true)}>
          <span style={{ color: C.wave, display: 'flex' }}><IconScreens /></span>
          <span style={{ flex: 1, textAlign: 'left' }}>
            <span style={{ display: 'block', fontSize: 30, fontWeight: 800 }}>전체 UI <span style={{ fontSize: 17, fontWeight: 500, color: C.darkSub }}>Full flow</span></span>
            <span style={{ display: 'block', fontSize: 18, color: '#c5ccd6', marginTop: 4 }}>처음부터 끝까지 화면 흐름 · 모형 화면(장치 동작 없음)</span>
          </span>
          <Chevron />
        </button>
        <div style={{ flex: 1, display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 14, minHeight: 0 }}>
          {UNITS.map((u) => (
            <button key={u.id} data-unit={u.id} onPointerDown={() => onPick(u.id)} style={tile(false)}>
              <span style={{ color: ICON_COLOR[u.id], display: 'flex', height: 70, alignItems: 'center' }}>{ICON[u.id]}</span>
              <span style={{ fontSize: 30, fontWeight: 800 }}>{u.title}</span>
              <span style={{ fontSize: 15, color: C.darkSub, marginTop: -6 }}>{u.en}</span>
              <span style={{ fontSize: 17, color: '#dfe4ea', lineHeight: 1.35 }}>{u.desc}</span>
              <span style={{ fontSize: 14, color: '#8b94a3', marginTop: 'auto' }}>{u.src}</span>
            </button>
          ))}
        </div>
        <div style={{ fontSize: 15, color: C.darkSub, textAlign: 'center' }}>
          장치는 각 화면에서 [시작]을 눌러야 움직입니다
        </div>
      </div>
    </Dark>
  )
}

const tile = (wide) => ({
  display: 'flex', flexDirection: wide ? 'row' : 'column', alignItems: wide ? 'center' : 'flex-start',
  gap: wide ? 22 : 8, padding: wide ? '0 26px' : '16px 18px 14px', height: wide ? 112 : '100%', minHeight: 0,
  background: C.darkCard, color: C.darkText, border: `1px solid ${C.darkLine}`, borderRadius: 14,
  fontFamily: 'inherit', textAlign: 'left', flexShrink: 0,
})

function StatusChips({ status }) {
  const s = status || {}
  const chip = (on, label, warn) => (
    <span style={{ display: 'inline-flex', alignItems: 'center', gap: 7, fontSize: 15, color: on ? '#cfe9d6' : warn ? '#f5d48a' : '#8b94a3' }}>
      <span style={{ width: 9, height: 9, borderRadius: '50%', background: on ? C.success : warn ? C.caution : '#4a5563' }} />
      {label}
    </span>
  )
  return (
    <div data-menu-status style={{ display: 'flex', alignItems: 'center', gap: 16 }}>
      {s.sim
        ? chip(false, s.bridge ? '모의 데이터 (개발 패널)' : '오프라인 · 모의 데이터', true)
        : <>
            {chip(s.bridge, s.bridge ? '젯슨 연결' : '젯슨 미연결')}
            {chip(s.teensy && s.fw !== false, !s.teensy ? '장치 미연결' : s.fw === false ? '장치 펌웨어 v1.1 아님' : '장치 연결')}
            {chip(s.radar, s.radar ? '레이더 수신' : '레이더 없음')}
          </>}
    </div>
  )
}

/* ---------- 공통 조각 ---------- */
function HeadRight({ live, linked, pv }) {
  return (
    <span style={{ display: 'flex', alignItems: 'center', gap: 14 }}>
      <SourceBadge live={live} linked={linked} />
      {pv && (
        <span data-phase={pv.phase} style={{ display: 'flex', alignItems: 'center', gap: 8, color: TONE[pv.tone], fontWeight: 700, fontSize: 19 }}>
          <span style={{ width: 11, height: 11, borderRadius: '50%', background: TONE[pv.tone],
            animation: pv.tone === 'run' || pv.tone === 'busy' ? 'pulsedot 1.4s infinite' : 'none' }} />
          {pv.text}
        </span>
      )}
    </span>
  )
}

/** 값 출처: 장치 실측이면 초록, 모의값이면 노랑. linked 는 브리지에 연결된 채 모의를 강제한 경우(개발 패널) */
export function SourceBadge({ live, linked = false }) {
  return live ? (
    <span data-source="live" style={{ fontSize: 15, fontWeight: 700, color: '#bfeccb', background: 'rgba(30,142,62,.22)',
      border: `1px solid ${C.success}`, borderRadius: 999, padding: '4px 12px' }}>실측 · 장치 연결</span>
  ) : (
    <span data-source="sim" data-sim style={{ fontSize: 15, fontWeight: 700, color: '#3d2f00', background: '#ffe9a8',
      border: `1px solid ${C.caution}`, borderRadius: 999, padding: '4px 12px' }}>{linked ? '모의 데이터 · 장치 명령 없음' : '모의 데이터 · 장치 미연결'}</span>
  )
}

function Band({ tone = 'note', children }) {
  const T = {
    note: ['rgba(255,255,255,.05)', '#aeb6c2'],
    danger: ['rgba(216,40,40,.14)', '#ff8a8a'],
    warn: ['rgba(242,96,0,.18)', '#FF8A3D'],
    ok: ['rgba(30,142,62,.22)', '#7be39a'],
  }[tone]
  return (
    <div data-band={tone} style={{ height: 44, display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 10,
      background: T[0], color: T[1], fontSize: 19, fontWeight: 700, flexShrink: 0, padding: '0 16px', textAlign: 'center' }}>
      {children}
    </div>
  )
}

function UStat({ label, value, sub, small = false }) {
  return (
    <div style={{ flex: 1, background: C.darkCard, borderRadius: 10, padding: small ? '7px 12px' : '8px 14px',
      display: 'flex', flexDirection: 'column', justifyContent: 'center', minHeight: 0 }}>
      <div style={{ fontSize: small ? 14 : 15, color: C.darkSub }}>{label}</div>
      <div data-stat={label} style={{ fontSize: small ? 25 : 34, fontWeight: 800, lineHeight: 1.08 }}>{value}</div>
      {sub && <div style={{ fontSize: 13, color: '#8b94a3', marginTop: 1 }}>{sub}</div>}
    </div>
  )
}

function BottomBar({ children }) {
  return (
    <div style={{ height: 76, display: 'flex', alignItems: 'center', gap: 12, padding: '0 16px', flexShrink: 0 }}>{children}</div>
  )
}

function StatusLine({ pv }) {
  return (
    <div data-unit-note style={{ flex: 1, minWidth: 0, fontSize: 17, color: pv.tone === 'warn' ? '#FF8A3D' : '#aeb6c2',
      textAlign: 'center', lineHeight: 1.3 }}>
      {pv.note || ''}
    </div>
  )
}

/** 길게 눌러야 실행되는 버튼(기본 1초). 짧게 닿으면 실행하지 않는다(관람객이 스쳐도 장치가 움직이지 않게) */
export function HoldBtn({ children, onDone, ms = 1000, disabled, w = 260, h = 58, fs = 21, color = C.success }) {
  const [p, setP] = useState(0)
  const timer = useRef(null)
  const raf = useRef(null)
  const cancel = () => {
    clearTimeout(timer.current)
    if (raf.current) cancelAnimationFrame(raf.current)
    raf.current = null
    setP(0)
  }
  const begin = (e) => {
    e.stopPropagation()
    if (disabled) return
    cancel()
    const t0 = Date.now()
    const step = () => { setP(Math.min(1, (Date.now() - t0) / ms)); raf.current = requestAnimationFrame(step) }
    raf.current = requestAnimationFrame(step)
    timer.current = setTimeout(() => { cancel(); if (onDone) onDone() }, ms)
  }
  useEffect(() => cancel, [])
  return (
    <button data-hold disabled={!!disabled} data-disabled={disabled ? '1' : undefined}
      onPointerDown={begin} onPointerUp={cancel} onPointerLeave={cancel} onPointerCancel={cancel}
      style={{ position: 'relative', overflow: 'hidden', width: w, height: h, borderRadius: 6, border: 'none',
        background: color, color: '#fff', fontSize: fs, fontWeight: 700, fontFamily: 'inherit', flexShrink: 0,
        ...(disabled ? { opacity: 0.4, cursor: 'not-allowed' } : {}) }}>
      <span style={{ position: 'absolute', left: 0, top: 0, bottom: 0, width: `${p * 100}%`, background: 'rgba(255,255,255,.3)' }} />
      <span style={{ position: 'relative' }}>{children}</span>
    </button>
  )
}

/* ---------- 시간, 횟수, 추이 ---------- */
/** 동작 중인 시간(초). 새로 시작하면 0 부터, 멈추면 마지막 값을 둔다 */
function useRunSeconds(running) {
  const [sec, setSec] = useState(0)
  useEffect(() => {
    if (!running) return undefined
    const t0 = Date.now()
    setSec(0)
    const iv = setInterval(() => setSec((Date.now() - t0) / 1000), 250)
    return () => clearInterval(iv)
  }, [running])
  return sec
}

/** 압박 횟수 추정(받은 압박 속도를 시간에 대해 더함). 시작 전에는 null */
function useCount(running, rate) {
  const [n, setN] = useState(null)
  const rateRef = useRef(rate); rateRef.current = rate
  useEffect(() => {
    if (!running) return undefined
    setN(0)
    let last = Date.now()
    const iv = setInterval(() => {
      const now = Date.now()
      const dt = (now - last) / 1000
      last = now
      setN((c) => countStep(c || 0, rateRef.current, dt))
    }, 250)
    return () => clearInterval(iv)
  }, [running])
  return n
}

/** 최근 값(0.2초 간격, 최대 n 개). 값이 없으면 null 로 남겨 선이 끊긴다 */
function useTrend(value, active, n = 60) {
  const [pts, setPts] = useState([])
  const vRef = useRef(value); vRef.current = value
  useEffect(() => {
    if (!active) return undefined
    setPts([])
    const iv = setInterval(() => setPts((p) => [...p.slice(-(n - 1)), vRef.current]), 200)
    return () => clearInterval(iv)
  }, [active])
  return pts
}

function TrendPanel({ label, values, target, unit, color = C.wave, n = 60 }) {
  const W = 600, H = 120, L = 6, R = 6, T = 8, B = 8
  const vs = values.filter((v) => typeof v === 'number')
  const top = Math.max(10, ...(target ? [target * 1.4] : []), ...vs.map((v) => v * 1.15))
  const sx = (i) => L + (i / (n - 1)) * (W - L - R)
  const sy = (v) => T + (1 - v / top) * (H - T - B)
  let d = ''
  values.forEach((v, i) => {
    if (typeof v !== 'number') return
    d += `${d && typeof values[i - 1] === 'number' ? 'L' : 'M'}${sx(i).toFixed(1)},${sy(v).toFixed(1)} `
  })
  const last = vs.length ? vs[vs.length - 1] : null
  return (
    <div style={{ flex: 1, background: C.darkPane, border: `1px solid ${C.darkLine}`, borderRadius: 10, padding: '9px 12px',
      display: 'flex', flexDirection: 'column', minHeight: 0 }}>
      <div style={{ fontSize: 15, color: C.darkSub, marginBottom: 3, display: 'flex', justifyContent: 'space-between' }}>
        <span>{label}</span>
        <span style={{ color: '#eef1f6', fontWeight: 700, fontSize: 18 }}>
          {last === null ? NA : `${last.toFixed(1)} ${unit}`}
          {target ? <span style={{ color: C.darkSub, fontWeight: 400, fontSize: 14 }}>  목표 {target} {unit}</span> : null}
        </span>
      </div>
      <svg viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none" style={{ flex: 1, width: '100%', minHeight: 0, display: 'block' }}>
        {target ? <line x1={L} x2={W - R} y1={sy(target)} y2={sy(target)} stroke="#8b94a3" strokeWidth="1.5" strokeDasharray="6 5" vectorEffect="non-scaling-stroke" /> : null}
        {d && <path data-trend d={d} fill="none" stroke={color} strokeWidth="3" vectorEffect="non-scaling-stroke" />}
      </svg>
    </div>
  )
}

/* ---------- 환자 상태 ---------- */
export function PatientUnit({ live, linked, tel, stale, radar, waves, onMenu, sim }) {
  const pv = patientView(tel, stale, radar)
  const [rosc, setRosc] = useState(false)
  const sec = useRunSeconds(true)
  let band
  // 장치는 회복을 판정하지 않는다. 맥박 신호가 잡히면 확인하라고 알리고, 회복 표시는 사람이 누른다(S9 와 같은 원칙)
  if (rosc) band = <Band tone="ok">자발순환 회복(ROSC) · 구조자 확인</Band>
  else if (pv.pulse) band = <Band tone="warn">맥박 신호가 잡힙니다 · 숨쉬기와 맥박을 확인하세요</Band>
  else band = <Band>손가락 센서의 맥박 신호를 기다리고 있습니다</Band>
  return (
    <Dark>
      <Header dark h={72} titleSize={26} title="환자 상태" titleEn="Patient Monitoring" right={<HeadRight live={live} linked={linked} />} />
      {band}
      <div style={{ flex: 1, display: 'flex', gap: 14, padding: '12px 16px', minHeight: 0 }}>
        <div style={{ flex: 1.6, display: 'flex', flexDirection: 'column', gap: 12, minHeight: 0 }}>
          <WavePanel label="호흡 / Respiration · 레이더 추정" d={pv.rrNum ? waves.breath : FLAT} color={C.breath}
            dur={waveDur(pv.rrNum, 600)} value={pv.rr} />
          <WavePanel label="손가락 센서 / Finger sensor" d={pv.pulse ? waves.ppg2 : FLAT} color={C.ppg}
            dur={waveDur(pv.hrNum, 240)} value={pv.hr} />
        </div>
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 9, minHeight: 0 }}>
          <UStat label="호흡수 / Respiration" value={pv.rr} sub={pv.radarOn ? '레이더 추정' : pv.radarStatus} />
          <UStat label="심박수 / Heart rate" value={pv.hr} sub="손가락 센서" />
          <UStat label="산소포화도 / SpO₂" value={pv.spo2} sub="손가락 센서" />
          <div style={{ display: 'flex', gap: 9 }}>
            <UStat small label="레이더 심박" value={pv.radarHr} />
            <UStat small label="측정 시간" value={clock(sec)} />
          </div>
        </div>
      </div>
      <BottomBar>
        <Btn variant="ghostDark" w={150} h={58} fs={21} onPress={onMenu}>메뉴</Btn>
        {sim ? (
          <div style={{ display: 'flex', gap: 8 }}>
            <SimToggle on={sim.st.breath} onPress={() => sim.set('breath', !sim.st.breath)}>모의 호흡</SimToggle>
            <SimToggle on={sim.st.finger} onPress={() => sim.set('finger', !sim.st.finger)}>모의 손가락</SimToggle>
          </div>
        ) : null}
        <div style={{ flex: 1 }} />
        {rosc
          ? <Btn variant="ghostDark" w={200} h={58} fs={21} onPress={() => setRosc(false)}>처음부터</Btn>
          : <Btn variant="warning" w={290} h={58} fs={21} onPress={() => setRosc(true)}>회복을 확인했습니다</Btn>}
      </BottomBar>
    </Dark>
  )
}

function SimToggle({ on, onPress, children }) {
  return (
    <button data-simtoggle aria-pressed={on} onPointerDown={(e) => { e.stopPropagation(); onPress() }}
      style={{ height: 44, padding: '0 14px', borderRadius: 999, fontSize: 16, fontWeight: 700, fontFamily: 'inherit',
        border: `1.5px solid ${on ? C.caution : '#4a5563'}`, background: on ? 'rgba(232,168,0,.18)' : 'transparent',
        color: on ? '#f5d48a' : '#aeb6c2' }}>
      {children} {on ? '켬' : '끔'}
    </button>
  )
}

/* ---------- 압박 ---------- */
export function CompressUnit({ live, linked, tel, stale, cfg, msg, waves, onStart, onStop, onMenu }) {
  const cv = compressView(tel, stale, cfg)
  const pv = phaseView(msg, 'compress')
  const running = cv.running
  const busy = BUSY.includes(pv.phase) || running
  const sec = useRunSeconds(running)
  const count = useCount(running, cv.rateNum)
  const trend = useTrend(cv.depthNum, running)
  const rate = running ? (cv.rateNum || cv.targetBpm) : null
  const targets = [cv.targetDepth ? `목표 깊이 ${cv.targetDepth} mm` : '목표 깊이 미설정',
    cv.targetBpm ? `속도 ${cv.targetBpm} 회/분` : null, cv.forceMax ? `힘 상한 ${cv.forceMax} N` : null].filter(Boolean).join(' · ')
  return (
    <Dark>
      <Header dark h={72} titleSize={26} title="압박" titleEn="Compression" right={<HeadRight live={live} linked={linked} pv={running && pv.phase === 'idle' ? { phase: 'running', text: '압박 중', tone: 'run' } : pv} />} />
      {running
        ? <Band tone="danger">압박 중 — 만지지 마세요&nbsp;<span style={{ color: '#c98' }}>/ Do not touch</span></Band>
        : <Band>{targets}</Band>}
      <div style={{ flex: 1, display: 'flex', gap: 14, padding: '12px 16px', minHeight: 0 }}>
        <div style={{ flex: 1.6, display: 'flex', flexDirection: 'column', gap: 12, minHeight: 0 }}>
          <WavePanel label="압박 리듬 / Compression rhythm" d={running ? waves.comp : FLAT} color={C.wave}
            dur={waveDur(rate, 150)} value={cv.rate} />
          <TrendPanel label="압박 깊이 추이 / Depth trend" values={trend} target={cv.targetDepth} unit="mm" />
        </div>
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 9, minHeight: 0 }}>
          <UStat label="압박 깊이 / Depth" value={cv.depth} sub={cv.targetDepth ? `목표 ${cv.targetDepth} mm` : '목표 미설정'} />
          <UStat label="압박 속도 / Rate" value={cv.rate} sub={cv.targetBpm ? `목표 ${cv.targetBpm} 회/분` : null} />
          <UStat label="압박 강도 / Force" value={cv.force}
            sub={[cv.forceKg, cv.forceMax ? `상한 ${cv.forceMax} N` : null].filter(Boolean).join(' · ') || null} />
          <div style={{ display: 'flex', gap: 9 }}>
            <UStat small label="경과 시간" value={clock(sec)} />
            <UStat small label="압박 횟수" value={count === null ? NA : `약 ${Math.floor(count)}회`} />
          </div>
        </div>
      </div>
      <BottomBar>
        <Btn variant="ghostDark" w={150} h={58} fs={21} onPress={onMenu}>메뉴</Btn>
        <StatusLine pv={pv} />
        <HoldBtn disabled={busy} onDone={onStart} w={260}>길게 눌러 압박 시작</HoldBtn>
        <Btn variant="danger" w={170} h={58} fs={23} disabled={!busy} onPress={onStop}>정지</Btn>
      </BottomBar>
    </Dark>
  )
}

/* ---------- 흡착 ---------- */
export function VacuumUnit({ live, linked, tel, stale, cfg, msg, onStart, onStop, onMenu }) {
  const vv = vacuumView(tel, stale, cfg)
  const pv = phaseView(msg, 'vacuum')
  const busy = BUSY.includes(pv.phase)
  const holdSec = useRunSeconds(vv.holding)
  let band
  if (vv.leak) band = <Band tone="warn">음압 누설이 감지됐습니다 · 패드와 바닥면을 확인하세요</Band>
  else if (vv.holding) band = <Band tone="ok">바닥 고정 완료 · 음압 유지 중</Band>
  else band = <Band>음압으로 장치를 바닥에 붙여, 압박할 때 장치가 들리지 않게 합니다</Band>
  return (
    <Dark>
      <Header dark h={72} titleSize={26} title="흡착 고정" titleEn="Vacuum Fixation" right={<HeadRight live={live} linked={linked} pv={pv} />} />
      {band}
      <div style={{ flex: 1, display: 'flex', gap: 14, padding: '12px 16px', minHeight: 0 }}>
        <div style={{ flex: 1.6, background: C.darkPane, border: `1px solid ${C.darkLine}`, borderRadius: 10,
          display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', minHeight: 0, padding: 10 }}>
          <div style={{ fontSize: 15, color: C.darkSub, alignSelf: 'flex-start', marginLeft: 6 }}>챔버 음압 / Chamber vacuum</div>
          <Gauge kPa={vv.kPa} target={vv.target} holding={vv.holding} />
        </div>
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 9, minHeight: 0 }}>
          <UStat label="현재 음압 / Vacuum" value={vv.kPaText} sub="대기압 기준(음수)" />
          <UStat label="목표 음압 / Target" value={vv.targetText} />
          <UStat label="고정 유지 시간" value={vv.holding || holdSec > 0 ? clock(holdSec) : NA} />
          <UStat label="누설 / Leak" value={vv.leakText} />
        </div>
      </div>
      <BottomBar>
        <Btn variant="ghostDark" w={150} h={58} fs={21} onPress={onMenu}>메뉴</Btn>
        <StatusLine pv={pv} />
        <Btn w={220} h={58} fs={22} fw={700} disabled={busy} onPress={onStart}>흡착 시작</Btn>
        <Btn variant="warning" w={170} h={58} fs={22} disabled={!busy && !vv.holding} onPress={onStop}>해제</Btn>
      </BottomBar>
    </Dark>
  )
}

/** 반원 게이지: 0 kPa(왼쪽) ~ -30 kPa(오른쪽). 목표선은 설정값이 있을 때만 */
function Gauge({ kPa, target, holding }) {
  const MIN = -30, cx = 200, cy = 196, r = 158
  const frac = (v) => Math.max(0, Math.min(1, v / MIN))
  const pt = (f, rr = r) => {
    const a = Math.PI * (1 - f)
    return [cx + rr * Math.cos(a), cy - rr * Math.sin(a)]
  }
  const arc = (f) => {
    const [x0, y0] = pt(0), [x1, y1] = pt(f)
    return `M${x0.toFixed(1)},${y0.toFixed(1)} A${r},${r} 0 0 1 ${x1.toFixed(1)},${y1.toFixed(1)}`
  }
  const has = typeof kPa === 'number'
  const f = has ? frac(kPa) : 0
  const tf = typeof target === 'number' ? frac(target) : null
  return (
    <svg viewBox="0 0 400 236" style={{ width: '100%', maxWidth: 520, flex: 1, minHeight: 0 }} role="img" aria-label="챔버 음압 게이지">
      <path d={arc(1)} fill="none" stroke="#2a3340" strokeWidth="26" strokeLinecap="round" />
      {has && f > 0.002 && <path data-gauge d={arc(f)} fill="none" stroke={holding ? C.success : C.wave} strokeWidth="26" strokeLinecap="round" />}
      {tf !== null && (() => {
        const [a0, b0] = pt(tf, r - 24), [a1, b1] = pt(tf, r + 24), [lx, ly] = pt(tf, r + 38)
        return (
          <g>
            <line x1={a0} y1={b0} x2={a1} y2={b1} stroke="#eef1f6" strokeWidth="3" />
            <text x={lx} y={ly} fontSize="14" fill="#c5ccd6" textAnchor="middle">목표 {target}</text>
          </g>
        )
      })()}
      <text x={cx} y={cy - 46} fontSize="54" fontWeight="800" fill="#eef1f6" textAnchor="middle">{has ? kPa.toFixed(1) : NA}</text>
      <text x={cx} y={cy - 14} fontSize="18" fill="#8b94a3" textAnchor="middle">kPa</text>
      <text x={cx - r} y={cy + 30} fontSize="14" fill="#8b94a3" textAnchor="middle">0</text>
      <text x={cx + r} y={cy + 30} fontSize="14" fill="#8b94a3" textAnchor="middle">-30</text>
    </svg>
  )
}

/* ---------- 아이콘 ---------- */
const ICON_COLOR = { patient: C.ppg, compress: C.wave, drive: '#f5b642', vacuum: C.breath }
const sv = { width: 58, height: 58, viewBox: '0 0 48 48', fill: 'none', stroke: 'currentColor', strokeWidth: 2.6,
  strokeLinecap: 'round', strokeLinejoin: 'round', 'aria-hidden': true }
function IconScreens() {
  return (
    <svg {...sv} width={64} height={64}>
      <rect x="5" y="8" width="38" height="26" rx="3" />
      <path d="M17 41h14M24 34v7" />
      <path d="M12 22h6l3-6 4 12 3-6h8" />
    </svg>
  )
}
const ICON = {
  patient: (
    <svg {...sv}>
      <path d="M24 41s-15-8.7-15-20a8.5 8.5 0 0 1 15-5.4A8.5 8.5 0 0 1 39 21c0 11.3-15 20-15 20z" />
      <path d="M12 24h7l3-5 4 9 3-4h7" />
    </svg>
  ),
  compress: (
    <svg {...sv}>
      <path d="M24 5v18M17 16l7 7 7-7" />
      <rect x="15" y="26" width="18" height="6" rx="2" />
      <path d="M6 40h36" />
    </svg>
  ),
  drive: (
    <svg {...sv}>
      <circle cx="14" cy="35" r="6" /><circle cx="34" cy="35" r="6" />
      <path d="M8 24h32" /><path d="M24 6v8M20 10h8" /><circle cx="24" cy="10" r="7" />
    </svg>
  ),
  vacuum: (
    <svg {...sv}>
      <path d="M8 30a16 16 0 0 1 32 0" /><path d="M24 30l7-9" />
      <path d="M6 40h36" /><path d="M12 36h24" />
    </svg>
  ),
}

function Chevron() {
  return (
    <svg width="30" height="30" viewBox="0 0 24 24" fill="none" stroke="#8b94a3" strokeWidth="2.4" aria-hidden="true">
      <path d="M9 5l7 7-7 7" />
    </svg>
  )
}
