import React from 'react'
import { C, SCREEN_W, SCREEN_H } from './tokens'

/* ============================================================
   공통 UI 컴포넌트
   - 디자인 원본의 인라인 스타일을 그대로 유지합니다.
   - 모든 버튼은 onPointerDown 으로 반응 → 터치 지연 없음
   ============================================================ */

const baseScreen = {
  width: SCREEN_W, height: SCREEN_H, overflow: 'hidden', position: 'relative',
  fontFamily: "'Pretendard','Apple SD Gothic Neo','Malgun Gothic',sans-serif",
  display: 'flex', flexDirection: 'column',
}

/** 라이트 테마 화면 래퍼 */
export function Light({ children, band, bandH = 6, style }) {
  return (
    <div style={{ ...baseScreen, background: C.lightBg, color: '#000', ...style }}>
      {band && <div style={{ height: bandH, background: band, flexShrink: 0 }} />}
      {children}
    </div>
  )
}

/** 다크 테마 화면 래퍼 */
export function Dark({ children, band, bandH = 12, style }) {
  return (
    <div style={{ ...baseScreen, background: C.darkBg, color: C.darkText, ...style }}>
      {band && <div style={{ height: bandH, background: band, flexShrink: 0 }} />}
      {children}
    </div>
  )
}

/** 전체 화면 경보(빨강 점멸) 래퍼 — 화면 15·16 */
export function AlarmRed({ children, style }) {
  return (
    <div data-safety-blink style={{ ...baseScreen, color: '#fff', animation: 'redalarm 1s infinite', ...style }}>
      {children}
    </div>
  )
}

/** 상단 헤더 바 */
export function Header({ title, titleEn, right, rightColor, dark = false, h = 80, titleSize = 28 }) {
  return (
    <div style={{
      height: h, display: 'flex', alignItems: 'center', justifyContent: 'space-between',
      padding: '0 28px', flexShrink: 0,
      borderBottom: dark ? '1px solid rgba(255,255,255,.1)' : `1px solid ${C.lightLine}`,
    }}>
      <div style={{ fontSize: titleSize, fontWeight: 700 }}>
        {title}
        {titleEn && <span style={{ fontSize: 18, fontWeight: 500, color: C.darkSub }}> / {titleEn}</span>}
      </div>
      {right && (
        <div style={{ fontSize: 20, fontWeight: rightColor ? 700 : 400, color: rightColor || (dark ? C.darkSub : C.lightSub) }}>
          {right}
        </div>
      )}
    </div>
  )
}

/** 점멸 상태 점 + 라벨 */
export function Dot({ color, label, size = 12, fast = false, fontSize = 20 }) {
  return (
    <div style={{ display: 'flex', alignItems: 'center', gap: 9, color, fontSize, fontWeight: 600 }}>
      <span style={{
        width: size, height: size, borderRadius: '50%', background: color, display: 'inline-block',
        animation: fast ? 'sblink .7s infinite' : 'pulsedot 1.4s infinite',
      }} />
      {label}
    </div>
  )
}

/** 불확정 진행 바 */
export function Progress({ w = 300, color = C.success, dur = '1.8s' }) {
  return (
    <div style={{ width: w, height: 6, borderRadius: 3, background: '#e6eaef', overflow: 'hidden', position: 'relative' }}>
      <div data-essential-motion style={{ position: 'absolute', left: 0, top: 0, height: '100%', width: '42%', borderRadius: 3, background: color, animation: `prog ${dur} linear infinite`, willChange: 'transform' }} />
    </div>
  )
}

/**
 * 터치 버튼
 * variant: primary | success | danger | warning | ghost | ghostDark | white | outlineWhite
 */
export function Btn({ children, onPress, variant = 'primary', w, h = 64, fs = 24, fw, flex, style }) {
  const V = {
    primary:      { background: C.primary, color: '#fff', border: 'none', fontWeight: 600 },
    success:      { background: C.success, color: '#fff', border: 'none', fontWeight: 700 },
    danger:       { background: C.danger,  color: '#fff', border: 'none', fontWeight: 700 },
    warning:      { background: C.warning, color: '#fff', border: 'none', fontWeight: 700 },
    ghost:        { background: '#fff', color: '#000', border: `1.5px solid ${C.lightLine}`, fontWeight: 400 },
    ghostBlue:    { background: '#fff', color: C.primary, border: `1.5px solid ${C.primary}`, fontWeight: 600 },
    ghostDark:    { background: 'transparent', color: C.darkText, border: '1.5px solid #4a5563', fontWeight: 600 },
    white:        { background: '#fff', color: C.danger, border: 'none', fontWeight: 800 },
    outlineWhite: { background: 'transparent', color: '#fff', border: '3px solid #fff', fontWeight: 800 },
  }[variant]

  return (
    <button
      onPointerDown={(e) => { e.stopPropagation(); onPress && onPress() }}
      style={{ width: w, height: h, flex, borderRadius: 6, fontSize: fs, fontFamily: 'inherit', ...V, ...(fw ? { fontWeight: fw } : {}), ...style }}
    >
      {children}
    </button>
  )
}

/** 화면 전체를 누르는 큰 터치 영역 (화면 10 압박 준비) */
export function PressArea({ children, onPress, style }) {
  return (
    <div
      data-press
      role="button"
      tabIndex={0}
      onPointerDown={onPress}
      onKeyDown={(e) => { if (e.key === 'Enter' || e.key === ' ') onPress() }}
      style={{ cursor: 'pointer', ...style }}
    >
      {children}
    </div>
  )
}

/** 스크롤되는 파형 그래프 (dur=null 이면 정지 화면) */
export function Wave({ d, color, dur = '5s', double = true }) {
  return (
    <div style={{ flex: 1, overflow: 'hidden', minHeight: 0 }}>
      <svg viewBox="0 0 2400 120" preserveAspectRatio="none"
        style={{ width: double ? '200%' : '100%', height: '100%', display: 'block', animation: dur ? `scrollwave ${dur} linear infinite` : 'none' }}>
        <path d={d} fill="none" stroke={color} strokeWidth="3" vectorEffect="non-scaling-stroke" />
      </svg>
    </div>
  )
}

/** 다크 테마 그래프 패널 */
export function WavePanel({ label, d, color, dur, pad = '9px 12px', labelSize = 15 }) {
  return (
    <div style={{ flex: 1, background: C.darkPane, border: `1px solid ${C.darkLine}`, borderRadius: 10, padding: pad, display: 'flex', flexDirection: 'column', minHeight: 0 }}>
      <div style={{ fontSize: labelSize, color: C.darkSub, marginBottom: 3 }}>{label}</div>
      <Wave d={d} color={color} dur={dur} />
    </div>
  )
}

/** MR60BHA2가 보낸 최근 60초 측정 추이. 값이 없으면 임의 파형을 만들지 않는다. */
export function VitalTrendPanel({ label, value, unit, history, field, color, min, max, status }) {
  const now = history.length ? history[history.length - 1].at : Date.now()
  const cutoff = now - 60_000
  let drawing = false
  let path = ''

  history.forEach((sample) => {
    if (sample.at < cutoff || sample[field] == null) {
      drawing = false
      return
    }
    const x = ((sample.at - cutoff) / 60_000) * 600
    const bounded = Math.max(min, Math.min(max, sample[field]))
    const y = 91 - ((bounded - min) / (max - min)) * 75
    path += `${drawing ? ' L' : ' M'} ${x.toFixed(1)} ${y.toFixed(1)}`
    drawing = true
  })

  const display = value == null ? '--' : Number(value).toFixed(0)
  const statusText = status === 'NORMAL' ? '정상' : status && status !== 'NO_DATA' ? status : '측정 대기'

  return (
    <div style={{ flex: 1, background: C.darkPane, border: `1px solid ${C.darkLine}`, borderRadius: 10, padding: '8px 12px', display: 'flex', flexDirection: 'column', minHeight: 0 }}>
      <div style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'space-between', gap: 12 }}>
        <div style={{ fontSize: 15, color: C.darkSub }}>{label}</div>
        <div style={{ display: 'flex', alignItems: 'baseline', gap: 7 }}>
          <span style={{ fontSize: 31, lineHeight: 1, fontWeight: 800, color, fontVariantNumeric: 'tabular-nums' }}>{display}</span>
          <span style={{ fontSize: 14, color: C.darkSub }}>{unit}</span>
          <span style={{ fontSize: 11, color: statusText === '정상' ? C.success : C.caution }}>{statusText}</span>
        </div>
      </div>
      <div style={{ flex: 1, minHeight: 0, position: 'relative', marginTop: 3 }}>
        <svg viewBox="0 0 600 108" preserveAspectRatio="none" style={{ width: '100%', height: '100%', display: 'block' }}>
          {[16, 41, 66, 91].map((y) => <line key={y} x1="0" y1={y} x2="600" y2={y} stroke="rgba(255,255,255,.09)" strokeWidth="1" />)}
          <text x="2" y="106" fill={C.darkSub} fontSize="10">−60초</text>
          <text x="575" y="106" fill={C.darkSub} fontSize="10">현재</text>
          {path && <path d={path} fill="none" stroke={color} strokeWidth="3" vectorEffect="non-scaling-stroke" strokeLinecap="round" strokeLinejoin="round" />}
        </svg>
        {!path && (
          <div style={{ position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', color: C.darkSub, fontSize: 14 }}>
            MR60BHA2 측정값 대기 중
          </div>
        )}
      </div>
    </div>
  )
}

/** 다크 테마 수치 카드 */
export function Stat({ label, value, big = true }) {
  return (
    <div style={{ flex: 1, background: C.darkCard, borderRadius: 10, padding: big ? '10px 14px' : '8px 12px', display: 'flex', flexDirection: 'column', justifyContent: 'center' }}>
      <div style={{ fontSize: big ? 16 : 14, color: C.darkSub }}>{label}</div>
      <div style={{ fontSize: big ? 38 : 26, fontWeight: big ? 800 : 700, lineHeight: 1.05 }}>{value}</div>
    </div>
  )
}
