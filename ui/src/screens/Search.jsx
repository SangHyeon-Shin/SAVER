import React, { useRef, useState } from 'react'
import { C } from '../tokens'
import { EV } from '../stateMachine'
import { Light, Header, Btn } from '../ui'
import { searchView, STEPS, mm } from '../search'

/* ================= 09 · 압박점 탐색 · X_SEARCH =================
   반력 곡선 F(x), 직전 N 점 평균(기준), 판정선(기준 × (1 − 감소율)), 말단, D, 압박점.
   연결 시: 브리지가 보낸 search 메시지만 그린다(UI 는 계산하지 않는다).
   미연결 시: searchDemo.js 의 모의 데이터. 화면에 '모의 데이터'로 표시한다.
   시작은 사용자가 '탐색 시작'을 눌러야 한다(진입만으로 장치가 움직이지 않는다). */

const INK = '#111827', SUB = '#4b5563', MUTED = '#6b7280', GRID = '#e5e7eb'

export function Screen09({ fire, ctx, search, searchCtl, setPatient }) {
  const v = searchView(search)
  const live = !!(searchCtl && searchCtl.live)
  const lastIdx = useRef(-1)
  if (v.running && v.stepIdx >= 0) lastIdx.current = v.stepIdx
  if (!search) lastIdx.current = -1

  const tried = !!search
  let action
  if (v.running) action = <Btn variant="warning" w={300} h={64} fs={25} onPress={() => searchCtl.stop()}>탐색 중지</Btn>
  else if (v.done) action = <Btn variant="success" w={300} h={64} fs={22} onPress={() => {}}>압박점 도달 · 다음 단계</Btn>
  else action = <Btn w={300} h={64} fs={25} fw={700} onPress={() => searchCtl.start()}>{tried ? '다시 탐색' : '탐색 시작'}</Btn>

  const status = v.done ? ['완료', C.success] : v.failed ? ['중단', C.warning]
    : v.running ? ['진행 중', C.primary] : ['대기', MUTED]

  return (
    <Light>
      <Header title="압박점 탐색" titleSize={28} right={
        <span style={{ display: 'flex', alignItems: 'center', gap: 14 }}>
          {!live && <SimBadge />}
          {v.encMode === 1 && <EncBadge />}
          <span style={{ display: 'flex', alignItems: 'center', gap: 8, color: status[1], fontWeight: 700 }}>
            <span style={{ width: 12, height: 12, borderRadius: '50%', background: status[1],
              animation: v.running ? 'pulsedot 1.4s infinite' : 'none' }} />
            {status[0]}
          </span>
        </span>} />

      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 12, padding: '14px 24px 18px', minHeight: 0 }}>
        <div style={{ flex: 1, display: 'flex', gap: 16, minHeight: 0 }}>
          <ChartCard v={v} />
          <div style={{ width: 340, display: 'flex', flexDirection: 'column', gap: 8, minHeight: 0 }}>
            <StepsCard v={v} lastIdx={lastIdx.current} />
            <ValuesCard v={v} />
            <PatientCard patient={ctx.patient} setPatient={setPatient} />
          </div>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 16 }}>
          <Btn variant="ghost" w={150} h={64} fs={23} onPress={() => fire(EV.CANCEL)}>취소</Btn>
          <div style={{ flex: 1, fontSize: 16, color: MUTED, lineHeight: 1.35, textAlign: 'center' }}>
            헤드가 가슴을 {v.rule && v.rule.press_mm ? v.rule.press_mm : '—'} mm 누른 채 이동하며 반력을 잽니다.<br />
            시작 전에 환자 정렬을 확인하세요. 탐색 결과는 인체에서 검증되지 않았습니다.
          </div>
          {action}
        </div>
      </div>
    </Light>
  )
}

/** 주행 엔코더 한쪽 + 자이로로 움직이는 임시 모드(초안 §4.7). 결과 표기에 남겨야 한다 */
function EncBadge() {
  return (
    <span data-enc1 style={{ fontSize: 15, fontWeight: 700, color: '#7a3e00', background: '#ffeedd',
      border: `1px solid ${C.warning}`, borderRadius: 999, padding: '4px 12px' }}>
      단일 엔코더 + 자이로 (임시)
    </span>
  )
}

function SimBadge() {
  return (
    <span data-sim style={{ fontSize: 15, fontWeight: 700, color: '#7a5a00', background: '#fff4cc',
      border: `1px solid ${C.caution}`, borderRadius: 999, padding: '4px 12px' }}>
      모의 데이터 · 장치 미연결
    </span>
  )
}

/* ---------- 반력 곡선 ---------- */
function ChartCard({ v }) {
  const [pick, setPick] = useState(null)
  const sel = pick !== null && v.pts[pick] ? v.pts[pick] : v.pts[v.pts.length - 1]
  const W = 600, H = 340

  return (
    <div style={{ width: 620, border: `1px solid ${C.lightLine}`, borderRadius: 10, padding: '10px 10px 6px',
      display: 'flex', flexDirection: 'column', position: 'relative', minHeight: 0 }}>
      <div style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'space-between', padding: '0 4px' }}>
        <div style={{ fontSize: 18, fontWeight: 700, color: INK }}>반력 F (N) · 주행 위치</div>
        <div style={{ fontSize: 14, color: MUTED }}>
          {sel ? `위치 ${sel.u.toFixed(0)} mm · F ${sel.F.toFixed(1)} N · 기준 ${sel.ref === null ? '—' : sel.ref.toFixed(1) + ' N'} · 감소 ${sel.r === null ? '—' : Math.round(sel.r * 100) + ' %'}`
            : '측정값 없음'}
        </div>
      </div>
      <Legend v={v} />
      <Chart v={v} W={W} H={H} pick={pick} setPick={setPick} />
      {!v.pts.length && <EmptyOverlay v={v} />}
    </div>
  )
}

function Legend({ v }) {
  const n = v.n === null ? 'N' : v.n
  const pct = v.ratio === null ? '' : ` × ${(1 - v.ratio).toFixed(1)}`
  const item = (sw, label) => (
    <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
      <svg width="22" height="12" aria-hidden="true">{sw}</svg>{label}
    </span>
  )
  return (
    <div style={{ display: 'flex', flexWrap: 'wrap', gap: '4px 14px', fontSize: 13, color: SUB, padding: '6px 4px 2px' }}>
      {item(<circle cx="11" cy="6" r="4.5" fill={C.primary} />, '반력 F')}
      {item(<line x1="1" y1="6" x2="21" y2="6" stroke={SUB} strokeWidth="2" />, `기준 (직전 ${n}점 평균)`)}
      {item(<line x1="1" y1="6" x2="21" y2="6" stroke={SUB} strokeWidth="1.5" strokeDasharray="4 3" />, `판정선 (기준${pct})`)}
      {item(<line x1="11" y1="0" x2="11" y2="12" stroke={INK} strokeWidth="2" />, '말단')}
      {item(<path d="M5 1 L17 1 L11 11 Z" fill={C.success} />, '압박점')}
    </div>
  )
}

function Chart({ v, W, H, pick, setPick }) {
  const L = 44, R = 14, T = 14, B = 46
  const pw = W - L - R, ph = H - T - B
  const { u0, u1, y1 } = v.domain
  const sx = (u) => L + ((u - u0) / (u1 - u0)) * pw
  const sy = (f) => T + ph - (f / y1) * ph
  const line = (arr, key) => arr.filter((p) => p[key] !== null)
    .map((p, i) => `${i ? 'L' : 'M'}${sx(p.u).toFixed(1)},${sy(p[key]).toFixed(1)}`).join(' ')

  const yt = []
  const ystep = y1 / 5
  for (let i = 0; i <= 5; i++) yt.push(Math.round(i * ystep))
  const xt = []
  for (let u = Math.ceil(u0 / 20) * 20; u <= u1; u += 20) xt.push(u)

  const onPoint = (e) => {
    if (!v.pts.length) return
    const box = e.currentTarget.getBoundingClientRect()
    const ux = u0 + ((e.clientX - box.left) / box.width * W - L) / pw * (u1 - u0)
    let best = 0
    v.pts.forEach((p, i) => { if (Math.abs(p.u - ux) < Math.abs(v.pts[best].u - ux)) best = i })
    setPick(best)
  }

  const tipX = v.tipU === null ? null : sx(v.tipU)
  const tgX = v.targetU === null ? null : sx(v.targetU)
  const hl = pick !== null && v.pts[pick] ? v.pts[pick] : null

  return (
    <svg viewBox={`0 0 ${W} ${H}`} width={W} height={H} style={{ display: 'block', touchAction: 'none' }}
      onPointerMove={onPoint} onPointerDown={onPoint} onPointerLeave={() => setPick(null)}
      role="img" aria-label="주행 위치에 따른 반력 곡선">
      {v.roi !== null && (
        <g>
          <rect x={sx(0)} y={T} width={sx(v.roi) - sx(0)} height={ph} fill="#f3f6fa" />
          <text x={sx(0) + 6} y={T + 14} fontSize="12" fill={MUTED}>탐색 구간</text>
        </g>
      )}
      {yt.map((y) => (
        <g key={'y' + y}>
          <line x1={L} x2={L + pw} y1={sy(y)} y2={sy(y)} stroke={y === 0 ? '#9ca3af' : GRID} strokeWidth="1" />
          <text x={L - 6} y={sy(y) + 4} fontSize="12" fill={MUTED} textAnchor="end">{y}</text>
        </g>
      ))}
      {xt.map((u) => (
        <text key={'x' + u} x={sx(u)} y={T + ph + 17} fontSize="12" fill={MUTED} textAnchor="middle">{u}</text>
      ))}
      <text x={L} y={H - 6} fontSize="13" fill={SUB}>← 머리</text>
      <text x={L + pw / 2} y={H - 6} fontSize="13" fill={SUB} textAnchor="middle">탐색 시작점에서 주행 거리 (mm)</text>
      <text x={L + pw} y={H - 6} fontSize="13" fill={SUB} textAnchor="end">발 →</text>

      <path d={line(v.pts, 'thr')} fill="none" stroke={SUB} strokeWidth="1.5" strokeDasharray="5 4" />
      <path d={line(v.pts, 'ref')} fill="none" stroke={SUB} strokeWidth="2" />
      <path d={line(v.pts, 'F')} fill="none" stroke={C.primary} strokeWidth="2" opacity=".5" />
      {v.pts.map((p, i) => (
        <circle key={i} data-pt cx={sx(p.u)} cy={sy(p.F)} r={4.5}
          fill={p.cand && p.u !== v.tipU ? '#fff' : C.primary} stroke={p.cand && p.u !== v.tipU ? C.primary : '#fff'}
          strokeWidth={p.cand && p.u !== v.tipU ? 2 : 1.5} />
      ))}
      {hl && <circle cx={sx(hl.u)} cy={sy(hl.F)} r={8} fill="none" stroke={INK} strokeWidth="1.5" />}

      {tipX !== null && (
        <g>
          <line x1={tipX} x2={tipX} y1={T + 22} y2={T + ph} stroke={INK} strokeWidth="2"
            strokeDasharray={v.tipFinal ? 'none' : '4 3'} />
          <text x={tipX + 5} y={T + ph - 8} fontSize="13" fontWeight="700" fill={INK}
            stroke="#fff" strokeWidth="3" paintOrder="stroke">{v.tipFinal ? '말단(추정)' : '첫 후보'}</text>
        </g>
      )}
      {tgX !== null && tipX !== null && (
        <g>
          <line x1={tgX} x2={tgX} y1={T + 22} y2={T + ph} stroke={C.success} strokeWidth="2" />
          <path d={`M${tgX - 7},${T + 8} L${tgX + 7},${T + 8} L${tgX},${T + 20} Z`} fill={C.success} />
          <text x={tgX - 6} y={T + ph - 8} fontSize="13" fontWeight="700" fill={INK} textAnchor="end"
            stroke="#fff" strokeWidth="3" paintOrder="stroke">압박점</text>
          <line x1={tgX} x2={tipX} y1={T + 34} y2={T + 34} stroke={INK} strokeWidth="1.5" />
          <path d={`M${tgX},${T + 34} l7,-4 v8 Z M${tipX},${T + 34} l-7,-4 v8 Z`} fill={INK} />
          <text x={(tgX + tipX) / 2} y={T + 29} fontSize="13" fontWeight="700" fill={INK} textAnchor="middle"
            stroke="#fff" strokeWidth="3" paintOrder="stroke">D {v.D === null ? '—' : v.D.toFixed(1) + ' mm'}</text>
        </g>
      )}
      {v.posU !== null && (v.running || v.done) && (
        <path data-pos d={`M${sx(v.posU)},${T + ph - 12} l-7,11 h14 Z`} fill={INK} opacity=".8" />
      )}
    </svg>
  )
}

function EmptyOverlay({ v }) {
  let title, sub, warn = false
  if (v.reason) { title = v.reason.title; sub = v.reason.sub; warn = true }
  else if (v.running) { title = '헤드를 내려 가슴에 접촉하는 중입니다'; sub = '접촉 뒤 정해진 깊이만큼 눌러 고정한 다음 주행합니다.' }
  else { title = '탐색 시작을 누르면 반력 곡선이 그려집니다'; sub = '머리 쪽에서 발 쪽으로 주행하며 반력이 급감하는 지점을 찾습니다.' }
  return (
    <div style={{ position: 'absolute', left: 64, right: 24, top: 120, display: 'flex', justifyContent: 'center' }}>
      <div data-overlay style={{ background: warn ? '#fff7ed' : 'rgba(255,255,255,.92)', border: `1px solid ${warn ? C.warning : C.lightLine}`,
        borderLeft: `6px solid ${warn ? C.warning : C.primary}`, borderRadius: 8, padding: '14px 18px', maxWidth: 470 }}>
        <div style={{ fontSize: 20, fontWeight: 700, color: INK }}>{title}</div>
        {sub && <div style={{ fontSize: 16, color: SUB, marginTop: 6, lineHeight: 1.4 }}>{sub}</div>}
      </div>
    </div>
  )
}

/* ---------- 진행 단계 ---------- */
function StepsCard({ v, lastIdx }) {
  const failAt = v.failed && v.moved ? Math.max(lastIdx, 0) : -1
  return (
    <div style={{ border: `1px solid ${C.lightLine}`, borderRadius: 10, padding: '10px 14px' }}>
      <div style={{ fontSize: 16, fontWeight: 700, color: INK, marginBottom: 4 }}>진행</div>
      {STEPS.map((s, i) => {
        let mark = '○', color = '#9ca3af', weight = 400
        if (v.done || (v.running && i < v.stepIdx) || (failAt >= 0 && i < failAt)) { mark = '✓'; color = C.success }
        if (v.running && i === v.stepIdx) { mark = '●'; color = C.primary; weight = 700 }
        if (v.phase === 'stopping' && i === lastIdx) { mark = '●'; color = C.warning; weight = 700 }
        if (failAt >= 0 && i === failAt) { mark = '✕'; color = C.warning; weight = 700 }
        let extra = ''
        if (s.phase === 'hold' && v.rule && v.rule.press_mm) extra = ` · ${v.rule.press_mm} mm`
        if (s.phase === 'scan' && v.scanTotal) extra = ` · ${v.pts.length} / ${v.scanTotal}`
        return (
          <div key={s.phase} style={{ display: 'flex', alignItems: 'center', gap: 10, height: 23, fontSize: 16, fontWeight: weight, color: i <= Math.max(v.stepIdx, failAt) || v.done ? INK : MUTED }}>
            <span style={{ width: 16, textAlign: 'center', color, animation: mark === '●' ? 'sblink 1.1s infinite' : 'none' }}>{mark}</span>
            {s.label}{extra}
          </div>
        )
      })}
    </div>
  )
}

/* ---------- 판정 값 ---------- */
function ValuesCard({ v }) {
  const row = (k, val, note, key) => (
    <div key={key} style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'space-between', height: 23 }}>
      <span style={{ fontSize: 15, color: SUB }}>{k}</span>
      <span style={{ fontSize: 17, fontWeight: 700, color: INK }}>
        {val}{note && <span style={{ fontSize: 13, fontWeight: 400, color: MUTED }}> {note}</span>}
      </span>
    </div>
  )
  const eapd = v.eapd !== null ? mm(v.eapd) : (v.reason && v.reason.code === 'EAPD_MISSING'
    ? <span style={{ color: C.warning }}>미측정</span> : '—')
  return (
    <div style={{ border: `1px solid ${C.lightLine}`, borderRadius: 10, padding: '8px 14px' }}>
      {row('EAPD (흉곽 전후 직경)', eapd, v.eapd !== null && v.eapdSim ? '모의' : '', 'e')}
      {row('보정 거리 D', mm(v.D), '', 'd')}
      {row(v.tipFinal ? '말단 (추정)' : '첫 후보', mm(v.tipU), '', 't')}
      {row('압박점', mm(v.targetU), v.targetU !== null ? '머리 쪽 D' : '', 'c')}
      {row('현재 위치', mm(v.posU), '', 'p')}
    </div>
  )
}

/* ---------- 환자 정보 (성인 대상 장치: 어린이 선택지 없음) ---------- */
function PatientCard({ patient, setPatient }) {
  const p = patient || {}
  const pick = (on) => ({
    flex: 1, height: 40, borderRadius: 6, fontSize: 17, fontFamily: 'inherit',
    border: on ? 'none' : `1.5px solid ${C.lightLine}`,
    background: on ? C.primary : '#fff', color: on ? '#fff' : '#000', fontWeight: on ? 600 : 400,
  })
  const set = (k, val) => setPatient && setPatient({ ...p, [k]: val })
  return (
    <div style={{ border: `1px solid ${C.lightLine}`, borderRadius: 10, padding: '8px 14px', display: 'flex', alignItems: 'center', gap: 8 }}>
      <span style={{ fontSize: 15, color: SUB, width: 34 }}>환자</span>
      <button style={pick(p.sex === 'male')} onPointerDown={() => set('sex', 'male')}>남성</button>
      <button style={pick(p.sex === 'female')} onPointerDown={() => set('sex', 'female')}>여성</button>
      <span style={{ width: 6 }} />
      <button style={pick(p.age === 'adult')} onPointerDown={() => set('age', 'adult')}>성인</button>
      <button style={pick(p.age === 'elder')} onPointerDown={() => set('age', 'elder')}>노인</button>
    </div>
  )
}
