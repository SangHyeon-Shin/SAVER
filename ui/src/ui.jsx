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
      <div style={{ position: 'absolute', top: 0, height: '100%', width: '42%', borderRadius: 3, background: color, animation: `prog ${dur} linear infinite` }} />
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

/** 다크 테마 수치 카드 */
export function Stat({ label, value, big = true }) {
  return (
    <div style={{ flex: 1, background: C.darkCard, borderRadius: 10, padding: big ? '10px 14px' : '8px 12px', display: 'flex', flexDirection: 'column', justifyContent: 'center' }}>
      <div style={{ fontSize: big ? 16 : 14, color: C.darkSub }}>{label}</div>
      <div style={{ fontSize: big ? 38 : 26, fontWeight: big ? 800 : 700, lineHeight: 1.05 }}>{value}</div>
    </div>
  )
}
