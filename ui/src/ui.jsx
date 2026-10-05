import React, { useContext } from 'react'
import { C, SCREEN_W, SCREEN_H } from './tokens'
import { VoiceCtx } from './voice/VoiceContext'

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
      <div style={{ display: 'flex', alignItems: 'center', gap: 16 }}>
        <VoiceButtons dark={dark} />
        {right && (
          <div style={{ fontSize: 20, fontWeight: rightColor ? 700 : 400, color: rightColor || (dark ? C.darkSub : C.lightSub) }}>
            {right}
          </div>
        )}
      </div>
    </div>
  )
}

/** 헤더의 음성 버튼: 다시 듣기, 소리 끄기/켜기 (음성은 보조. 재생 실패는 제어와 무관) */
function VoiceButtons({ dark }) {
  const vc = useContext(VoiceCtx)
  if (!vc) return null
  const fg = dark ? C.darkText : '#1a1f29'
  const line = dark ? '#4a5563' : C.lightLine
  const b = { height: 40, display: 'flex', alignItems: 'center', gap: 6, padding: '0 10px', borderRadius: 8,
    border: `1.5px solid ${line}`, background: 'transparent', color: fg, fontSize: 15, fontWeight: 600, fontFamily: 'inherit' }
  const spk = (
    <path d="M3 9 H7 L12 5 V19 L7 15 H3 Z" fill="currentColor" />
  )
  return (
    <div data-voice style={{ display: 'flex', gap: 6 }}>
      <button aria-label="음성 다시 듣기" style={b} disabled={vc.muted}
        onPointerDown={(e) => { e.stopPropagation(); vc.voice.replay() }}>
        <svg width="20" height="20" viewBox="0 0 24 24" aria-hidden="true">{spk}
          <path d="M15.5 8.5 a5 5 0 0 1 0 7 M18 6 a8.5 8.5 0 0 1 0 12" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" /></svg>
        다시 듣기
      </button>
      <button aria-label={vc.muted ? '소리 켜기' : '소리 끄기'} aria-pressed={vc.muted} style={{ ...b, width: 44, justifyContent: 'center', padding: 0,
        ...(vc.muted ? { background: dark ? '#3a4250' : '#eef1f5' } : {}) }}
        onPointerDown={(e) => { e.stopPropagation(); vc.toggleMute() }}>
        <svg width="20" height="20" viewBox="0 0 24 24" aria-hidden="true">{spk}
          {vc.muted
            ? <path d="M16 9 L22 15 M22 9 L16 15" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
            : <path d="M15.5 8.5 a5 5 0 0 1 0 7" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />}</svg>
      </button>
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

/** 화면 안 단계 표시 (한 화면에 지시 하나, 10/5). n 단계 중 i 번째(0부터) */
export function StepDots({ n, i, dark = false }) {
  const on = dark ? '#e6ebf2' : C.primary
  const off = dark ? 'rgba(255,255,255,.22)' : '#c9d2de'
  return (
    <div data-steps={`${i + 1}/${n}`} style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
      {Array.from({ length: n }, (_, k) => (
        <span key={k} style={{ width: k === i ? 26 : 10, height: 10, borderRadius: 5, background: k <= i ? on : off, transition: 'width .2s' }} />
      ))}
      <span style={{ marginLeft: 6, fontSize: 17, fontWeight: 700, color: dark ? '#b9c2cf' : '#6b7280' }}>{i + 1} / {n}</span>
    </div>
  )
}

/** 화면 안 단계 상태. 단계가 바뀌면 그 단계 음성(when 'step:N')을 앞 문구를 끊고 재생한다.
    처음 보이는 단계의 음성은 화면 진입 문구(enter, 오류면 position_error)가 맡는다 */
export function useSteps(n, start = 0) {
  const vc = useContext(VoiceCtx)
  const [i, setI] = React.useState(start)
  const first = React.useRef(true)
  React.useEffect(() => {
    if (first.current) { first.current = false; return }   // 진입 음성은 App 이 재생
    if (vc) vc.voice.cue('step:' + (i + 1))
  }, [i])
  return [i, (k) => setI(Math.max(0, Math.min(n - 1, k)))]
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
