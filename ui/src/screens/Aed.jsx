import React, { useEffect, useState, useContext } from 'react'
import { C } from '../tokens'
import { EV } from '../stateMachine'
import { Light, Dark, Header, Btn, Dot } from '../ui'
import { VoiceCtx } from '../voice/VoiceContext'

/* ============================================================
   AED 병행 (state_machine_v2.json v2.2)
   - 21 X_AED_PAD: 가슴이 드러난 때(겉옷 제거 뒤) AED 패드 부착 확인. 없으면 건너뛴다
   - 22 X_AED   : 압박 중·일시정지 중 구조자가 [AED 사용]을 누를 때만 들어온다(PAUSE_CPR)
                  자동 재개 없음. 구조자가 [압박 재개]를 눌러야 S7(RESUME_CPR)
                  압박 중단 경고 기준은 의학 근거로 정한다(aed_pause_warn_s, 비어 있으면 경고 안 함)
   한건민·한성윤 검토 전.
   ============================================================ */

/** AED 표지: 하트와 번개 */
export function AedMark({ size = 64, color = C.success }) {
  return (
    <svg width={size} height={size} viewBox="0 0 64 64" role="img" aria-label="자동심장충격기">
      <path d="M32 56 C 10 40, 4 28, 8 18 C 12 8, 26 6, 32 16 C 38 6, 52 8, 56 18 C 60 28, 54 40, 32 56 Z" fill={color} />
      <path d="M35 14 L 24 34 L 32 34 L 28 50 L 42 28 L 34 28 L 38 14 Z" fill="#fff" />
    </svg>
  )
}

/* ================= 21 · AED 패드 부착 확인 · X_AED_PAD ================= */
export function Screen21({ fire }) {
  return (
    <Light>
      <Header title="자동심장충격기(AED) 패드 붙이기" right="가슴이 드러난 지금" />
      <div style={{ flex: 1, position: 'relative', display: 'flex', alignItems: 'center', gap: 34, padding: '0 48px 70px' }}>
        <div style={{ width: 200, height: 200, borderRadius: 24, background: '#eaf6ee', display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0 }}>
          <AedMark size={128} />
        </div>
        <div style={{ flex: 1 }}>
          <div style={{ fontSize: 40, fontWeight: 800, lineHeight: 1.3 }}>자동심장충격기가 있으면<br />지금 패드를 붙이세요</div>
          <div style={{ fontSize: 21, color: '#3a4250', marginTop: 16, lineHeight: 1.55 }}>
            패드를 붙일 곳은 패드에 그려진 그림을 따르세요.<br />
            가슴 가운데 아래쪽(압박 위치)은 비워 두세요.
          </div>
          <div style={{ fontSize: 18, color: '#6b7280', marginTop: 12, lineHeight: 1.5 }}>
            아직 없으면 [아직 없음]을 누르고 진행하세요. 압박 중에 도착하면 압박 화면의 [자동심장충격기 사용]을 누릅니다.
          </div>
        </div>
        <Btn variant="ghost" w={150} fs={23} onPress={() => fire(EV.CANCEL)}
          style={{ position: 'absolute', left: 28, bottom: 24 }}>취소</Btn>
        <div style={{ position: 'absolute', right: 28, bottom: 24, display: 'flex', gap: 12 }}>
          <Btn variant="ghostBlue" w={230} fs={22} onPress={() => fire(EV.AED_NONE)}>아직 없음</Btn>
          <Btn variant="success" w={270} fs={24} onPress={() => fire(EV.AED_PADS_ON)}>패드를 붙였습니다</Btn>
        </div>
      </div>
    </Light>
  )
}

const mmss = (s) => `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`

/* ================= 22 · AED 분석 · 충격 (압박 일시정지) · X_AED ================= */
export function Screen22({ fire, ctx, aedWarnS }) {
  const [sec, setSec] = useState(0)
  useEffect(() => {
    const t0 = Date.now()
    const id = setInterval(() => setSec(Math.floor((Date.now() - t0) / 1000)), 250)
    return () => clearInterval(id)
  }, [])
  const warnSet = typeof aedWarnS === 'number' && aedWarnS > 0
  const over = warnSet && sec >= aedWarnS
  const padsOn = ctx.aedPads === true
  // 장치 음성은 진입 1회 뒤 끈다. 경고 기준이 정해져 있고 넘었을 때만 V39 (voice/plan.js)
  const vc = useContext(VoiceCtx)
  useEffect(() => { if (over && vc) vc.voice.event('aed_pause_warn') }, [over])

  return (
    <Dark band={over ? C.danger : C.caution} bandH={14}>
      <Header dark h={72} titleSize={26} title="자동심장충격기 사용 중" titleEn="AED in use"
        right="압박 멈춤" rightColor={C.caution} />
      <div style={{ flex: 1, display: 'flex', gap: 22, padding: '20px 26px 0', minHeight: 0 }}>
        <div style={{ width: 360, background: C.darkCard, borderRadius: 12, padding: '18px 20px', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 6 }}>
          <div style={{ fontSize: 20, color: C.darkSub }}>압박을 멈춘 시간</div>
          <div data-aed-timer style={{ fontSize: 92, fontWeight: 800, lineHeight: 1, fontVariantNumeric: 'tabular-nums', color: over ? '#ff8a8a' : C.darkText }}>{mmss(sec)}</div>
          {over && (
            <div data-safety-blink style={{ fontSize: 19, fontWeight: 700, color: '#ff8a8a', animation: 'sblink 1s infinite', textAlign: 'center' }}>
              가슴 압박을 멈춘 시간이 길어지고 있습니다
            </div>
          )}
          <div style={{ fontSize: 14, color: '#7c8492', marginTop: 6, textAlign: 'center' }}>
            {warnSet ? `경고 기준 ${aedWarnS}초` : '경고 기준 미설정 (의학 근거 확인 전)'}
          </div>
        </div>
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 12 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 14 }}>
            <AedMark size={58} />
            <div style={{ fontSize: 34, fontWeight: 800, lineHeight: 1.2 }}>자동심장충격기의<br />음성 안내를 따르세요</div>
          </div>
          {[
            ['분석과 충격 중에는 환자에게서 손을 떼세요.', true],
            ['자동심장충격기가 가슴 압박을 다시 하라고 하면 [압박 재개]를 누르세요.', true],
            [padsOn ? '패드: 붙임 확인됨' : '패드를 아직 붙이지 않았다면 지금 붙이세요. 위치는 패드 그림을 따릅니다.', !padsOn],
          ].map(([t, strong], i) => (
            <div key={i} style={{ background: C.darkCard, borderRadius: 10, padding: '13px 16px', display: 'flex', alignItems: 'center', gap: 12 }}>
              <span style={{ width: 10, height: 10, borderRadius: '50%', background: strong ? C.caution : C.success, flexShrink: 0 }} />
              <div style={{ fontSize: 21, fontWeight: strong ? 700 : 500, color: strong ? C.darkText : '#b9c2cf' }}>{t}</div>
            </div>
          ))}
          <div style={{ marginTop: 2 }}><Dot color={C.darkSub} label="이 장치의 음성은 꺼집니다 · 자동심장충격기 안내를 따르세요" size={9} fontSize={16} /></div>
        </div>
      </div>
      <div style={{ height: 96, display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '0 26px', flexShrink: 0 }}>
        <div style={{ fontSize: 17, color: C.darkSub }}>자동으로 다시 시작하지 않습니다.</div>
        <div style={{ display: 'flex', gap: 12 }}>
          <Btn variant="ghostDark" w={170} h={66} fs={23} fw={400} onPress={() => fire(EV.STOP)}>정지</Btn>
          <Btn w={300} h={66} fs={26} fw={700} onPress={() => fire(EV.AED_RESUME)}>압박 재개</Btn>
        </div>
      </div>
    </Dark>
  )
}
