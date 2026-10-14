import React, { useState, useEffect, useContext } from 'react'
import { C } from '../tokens'
import { EV } from '../stateMachine'
import { Light, Dark, Header, Btn, StepDots, useSteps } from '../ui'

import { IMG } from '../images'
import { VoiceCtx } from '../voice/VoiceContext'

/* ================= 17 · 수동 해제 · S12 · UI-12 · 주의 =================
   한 화면에 지시 하나(10/5): ① 바닥 고정이 풀릴 때까지 기다리기 → ② 장치를 환자에게서 치우기 → [완료].
   진입 때 장치가 음압 해제 명령(VACUUM_OFF)을 보내므로 구조자는 기다린다(한성윤 검토 10/12).
   음성 V27(진입). V42는 해제 완료 판정(한건민) 전까지 보류(10/14 결정 ⑤) */
export function Screen17({ fire }) {
  const [i, go] = useSteps(2)
  const step = (n, imgAlt, extra) => (
    <div style={{ width: 260, height: 170, background: '#fff', border: `${n === i + 1 ? 3 : 1}px solid ${n === i + 1 ? C.caution : C.lightLine}`, borderRadius: 10, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 10, position: 'relative', opacity: n === i + 1 ? 1 : 0.35 }}>
      <img src={IMG.device} alt={imgAlt} style={{ maxWidth: '100%', maxHeight: '100%', objectFit: 'contain', display: 'block', ...(n === 2 ? { transform: 'translateY(-6px)' } : {}) }} />
      <span style={{ position: 'absolute', left: 8, top: 6, width: 28, height: 28, borderRadius: '50%', background: C.caution, color: '#fff', font: '700 16px sans-serif', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>{n}</span>
      {extra}
    </div>
  )

  return (
    <Light band={C.caution} bandH={12}>
      <Header title="수동 해제" right="주의" rightColor="#9a7000" h={72} />
      <div style={{ flex: 1, position: 'relative', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 22, paddingBottom: 80 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 18 }}>
          {step(1, '바닥 고정이 풀리기를 기다림')}
          <div style={{ fontSize: 30, color: '#8a929c' }}>→</div>
          {step(2, '장치 들어올리기',
            <span style={{ position: 'absolute', right: 10, bottom: 8, color: C.success, fontSize: 24, fontWeight: 800, animation: 'pulsedot 1.3s infinite' }}>↑</span>)}
        </div>
        <div style={{ fontSize: 46, fontWeight: 800 }}>{i === 0 ? '바닥 고정이 풀릴 때까지 기다리세요' : '장치를 환자에게서 치우세요'}</div>
        {i > 0 && <Btn variant="ghost" w={160} fs={23} onPress={() => go(0)}
          style={{ position: 'absolute', left: 28, bottom: 24 }}>이전</Btn>}
        <div style={{ position: 'absolute', left: 0, right: 0, bottom: 44, display: 'flex', justifyContent: 'center', pointerEvents: 'none' }}>
          <StepDots n={2} i={i} />
        </div>
        <Btn w={240} fs={24} fw={700} onPress={() => (i === 0 ? go(1) : fire(EV.RELEASE_DONE))}
          style={{ position: 'absolute', right: 28, bottom: 24 }}>{i === 0 ? '다음' : '완료'}</Btn>
      </div>
    </Light>
  )
}

/* ================= 18 · 종료 / 기록 저장 · S13 · UI-13 · 정상 ================= */
export function Screen18({ fire, ctx }) {
  return (
    <Light band={C.success}>
      <Header title="작동 종료" right="정상 종료" rightColor={C.success} h={74} />
      <div style={{ flex: 1, position: 'relative', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 18 }}>
        <div style={{ width: 96, height: 96, borderRadius: '50%', background: C.success, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          <div style={{ width: 18, height: 34, border: 'solid #fff', borderWidth: '0 7px 7px 0', transform: 'rotate(45deg) translate(-2px,-3px)' }} />
        </div>
        <div style={{ fontSize: 44, fontWeight: 800 }}>작동을 마쳤습니다</div>
        <div style={{ fontSize: 22, color: '#3a4250' }}>기록이 저장되었습니다.</div>
        <div style={{ fontSize: 16, color: '#8a929c' }}>기록 {ctx.log.length}건 저장됨</div>
        <Btn variant="ghost" w={200} fs={23} onPress={() => fire(EV.BACK_TO_IDLE)}
          style={{ position: 'absolute', left: 28, bottom: 24 }}>대기로</Btn>
        <Btn w={200} fs={23} onPress={() => fire(EV.POWER_OFF)}
          style={{ position: 'absolute', right: 28, bottom: 24 }}>전원 끄기</Btn>
      </div>
    </Light>
  )
}

/* ================= 19 · 인공호흡 가이드 · 주의 · 한/영 병기 =================
   1~4단계는 시간에 따라 넘어가는 안내다. 5단계에서 멈추고, 구조자가 손을 뗀 뒤
   [압박 재개]를 눌러야 압박으로 돌아간다(시간이 끝났다고 자동으로 재개하지 않는다).
   인공호흡 계획에 마스크가 없어 마스크 단계를 뺐다(10/14 팀장). 문구는 한성윤 검토(10/12)를 따랐고,
   단계 시간은 검토 전이다. 단계 음성 V29, V31~V33은 교육용이라 재생하지 않고 마지막 단계 V34만 말한다(10/14 결정 ③). */
const VENT_STEPS = [
  { n: '①', k: '머리 젖히기', ko: '머리를 젖히고\n턱을 들어 올리세요',      en: 'Tilt the head, lift the chin', sec: 3 },
  { n: '②', k: '숨 불어넣기', ko: '가슴이 올라올 만큼\n1초 동안 불어넣으세요', en: 'Breathe in for 1 second until the chest rises', sec: 1 },
  { n: '③', k: '가슴 확인',    ko: '가슴이 내려오는지\n확인하세요',          en: 'Check that the chest falls',  sec: 2 },
  { n: '④', k: '한 번 더',    ko: '같은 방법으로\n한 번 더 불어넣으세요',    en: 'Give a second breath the same way', sec: 1 },
  { n: '⑤', k: '압박 재개',   ko: '손을 떼고\n[압박 재개]를 누르세요',      en: 'Hands off, then press Resume', sec: 0 },
]

const LAST = VENT_STEPS.length - 1

/* v2.3: 진입 때 PAUSE_CPR 를 보낸다. 장치가 멈춤(CPR_PAUSED)을 보고하기 전에는 안내를 시작하지 않고 기다린다.
   v2.4: [정지](STOP → S11)는 멈춤 보고 전에도 언제든 누를 수 있다(한건민 합의 10/5) */
export function Screen19({ fire, paused = true }) {
  if (!paused) return <VentWait fire={fire} />
  return <VentGuide fire={fire} />
}

function VentWait({ fire }) {
  return (
    <Dark band={C.caution} bandH={12}>
      <Header dark h={70} titleSize={25} title="인공호흡 안내" titleEn="Rescue Breathing" right="기다림" rightColor={C.caution} />
      <div data-pause-state="pausing" style={{ flex: 1, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 14 }}>
        <div style={{ fontSize: 30, fontWeight: 700, color: '#c5ccd6' }}>압박을 멈추는 중입니다</div>
        <div style={{ fontSize: 46, fontWeight: 800 }}>잠시 기다리세요</div>
        <Btn variant="ghostDark" w={180} h={58} fs={22} fw={600} onPress={() => fire(EV.STOP)}>정지</Btn>
      </div>
    </Dark>
  )
}

function VentGuide({ fire }) {
  const [i, setI] = useState(0)
  const [left, setLeft] = useState(VENT_STEPS[0].sec)
  const vc = useContext(VoiceCtx)

  useEffect(() => {
    if (vc) vc.voice.event('step:' + (i + 1))   // 단계 음성은 마지막 단계 V34만(V29, V31~V33은 교육용, 10/14)
    setLeft(VENT_STEPS[i].sec)
    if (i === LAST) return undefined          // 마지막 단계는 구조자 버튼으로만 넘어간다
    const tick = setInterval(() => setLeft((v) => Math.max(0, v - 1)), 1000)
    const advance = setTimeout(() => setI(i + 1), VENT_STEPS[i].sec * 1000)
    return () => { clearInterval(tick); clearTimeout(advance) }
  }, [i])

  const s = VENT_STEPS[i]
  return (
    <Dark band={C.caution} bandH={12}>
      <Header dark h={70} titleSize={25} title="인공호흡 안내" titleEn="Rescue Breathing"
        right={`${i + 1} / ${VENT_STEPS.length}`} rightColor={C.caution} />
      <div style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 44, minHeight: 0 }}>
        <div style={{ position: 'relative', width: 170, height: 170, flexShrink: 0 }}>
          <svg width="170" height="170" viewBox="0 0 120 120">
            <circle cx="60" cy="60" r="54" fill="none" stroke="#2a3240" strokeWidth="10" />
            <circle cx="60" cy="60" r="54" fill="none" stroke={C.wave} strokeWidth="10" strokeLinecap="round"
              strokeDasharray="339.292" transform="rotate(-90 60 60)"
              style={{ animation: `ringcount ${s.sec}s linear infinite` }} />
          </svg>
          <div style={{ position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 40, fontWeight: 800 }}>{i === LAST ? '손 떼기' : left + '초'}</div>
        </div>
        <div style={{ minWidth: 330 }}>
          <div style={{ fontSize: 30, fontWeight: 800, lineHeight: 1.3, whiteSpace: 'pre-line' }}>{s.ko}</div>
          <div style={{ fontSize: 18, color: '#9aa3b0', marginTop: 10 }}>{s.en}</div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8, fontSize: 16, color: C.caution, marginTop: 14 }}>
            <span style={{ width: 9, height: 9, borderRadius: '50%', background: C.caution, display: 'inline-block' }} />
            너무 빠르거나 세게 불어넣으면 경고가 나옵니다
          </div>
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
          {i === LAST
            ? <Btn w={200} h={64} fs={24} fw={700} onPress={() => fire(EV.VENT_DONE)}>압박 재개</Btn>
            : <Btn variant="ghostDark" w={200} h={60} fs={20} fw={600} onPress={() => setI(LAST)}>건너뛰기</Btn>}
          <Btn variant="ghostDark" w={200} h={52} fs={20} fw={600} onPress={() => fire(EV.STOP)}>정지</Btn>
        </div>
      </div>
      <div style={{ height: 120, borderTop: '1px solid rgba(255,255,255,.1)', display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '0 26px', flexShrink: 0 }}>
        {VENT_STEPS.map((v, k) => {
          const on = k === i
          return (
            <div key={k} style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 6, width: '15%' }}>
              <span style={{
                width: on ? 40 : 34, height: on ? 40 : 34, borderRadius: '50%',
                background: on ? C.caution : '#2a3240', color: on ? C.darkBg : '#9aa3b0',
                display: 'flex', alignItems: 'center', justifyContent: 'center',
                fontWeight: on ? 800 : 700, fontSize: on ? 19 : 16,
                boxShadow: on ? '0 0 0 4px rgba(232,168,0,.25)' : 'none',
              }}>{v.n}</span>
              <span style={{ fontSize: on ? 13 : 12, color: on ? C.caution : C.darkSub, fontWeight: on ? 700 : 400, textAlign: 'center' }}>{v.k}</span>
            </div>
          )
        })}
      </div>
    </Dark>
  )
}

/* ================= 20 · 배터리 부족 (1) 전체 알림 팝업 ================= */
export function BatteryPopup({ onClose }) {
  return (
    <div onPointerDown={onClose} style={{
      position: 'absolute', inset: 0, zIndex: 40, background: 'rgba(10,13,18,.62)',
      display: 'flex', alignItems: 'center', justifyContent: 'center',
    }}>
      <div style={{ width: 380, background: '#fff', borderRadius: 14, boxShadow: '0 10px 40px rgba(0,0,0,.4)', overflow: 'hidden', textAlign: 'center' }}>
        <div style={{ height: 10, background: C.caution }} />
        <div style={{ padding: '30px 26px' }}>
          <div style={{ display: 'inline-flex', alignItems: 'center', marginBottom: 18, animation: 'sblink 1s infinite' }}>
            <div style={{ width: 78, height: 44, border: `3px solid ${C.caution}`, borderRadius: 6, display: 'flex', alignItems: 'center', padding: 4 }}>
              <div style={{ width: '22%', height: '100%', background: C.caution, borderRadius: 2 }} />
            </div>
            <div style={{ width: 7, height: 18, background: C.caution, borderRadius: '0 3px 3px 0' }} />
          </div>
          <div style={{ fontSize: 24, fontWeight: 800, color: '#000' }}>배터리가 부족합니다</div>
          <div style={{ fontSize: 18, color: '#4a5260', marginTop: 10, lineHeight: 1.45 }}>충전 또는 교체가 필요합니다.</div>
          <div style={{ fontSize: 15, color: '#8a929c', marginTop: 16 }}>화면을 눌러 계속 진행</div>
        </div>
      </div>
    </div>
  )
}

/* ================= 20 · 배터리 부족 (2) 상시 표시 뱃지 ================= */
export function BatteryBadge() {
  return (
    <div style={{
      position: 'absolute', right: 14, top: 10, zIndex: 30,
      display: 'flex', alignItems: 'center', gap: 6,
      background: 'rgba(232,168,0,.16)', border: `1px solid ${C.caution}`, borderRadius: 6,
      padding: '5px 9px', animation: 'sblink 1.2s infinite', pointerEvents: 'none',
    }}>
      <div style={{ width: 26, height: 15, border: `2px solid ${C.caution}`, borderRadius: 3, display: 'flex', alignItems: 'center', padding: 2 }}>
        <div style={{ width: '22%', height: '100%', background: C.caution, borderRadius: 1 }} />
      </div>
      <span style={{ fontSize: 13, color: C.caution, fontWeight: 700 }}>부족</span>
    </div>
  )
}
