import React from 'react'
import { C } from '../tokens'
import { EV } from '../stateMachine'
import { Light, Header, Btn, Dot, Progress, PressArea, StepDots, useSteps } from '../ui'
import { AedMark } from './Aed'

import { IMG } from '../images'

/* ================= 01 · 부팅 / 시스템 점검 · S1 · UI-01 ================= */
export function Screen01() {
  return (
    <Light style={{ alignItems: 'center', justifyContent: 'center' }}>
      <div style={{ width: 400, height: 250, display: 'flex', alignItems: 'center', justifyContent: 'center', marginBottom: 20 }}>
        <img src={IMG.device} alt="SAVER 장치" style={{ maxWidth: '100%', maxHeight: '100%', objectFit: 'contain', display: 'block' }} />
      </div>
      <div style={{ fontSize: 66, fontWeight: 800, letterSpacing: '.05em', lineHeight: 1 }}>SAVER</div>
      {/* 원본 디자인에는 'Smart AI-Vacuum'으로 되어 있으나 팀 공식 명칭으로 통일 */}
      <div style={{ fontSize: 22, marginTop: 8 }}>Smart Auto-positioning Vacuum Emergency Resuscitator</div>
      <div style={{ marginTop: 38 }}><Progress w={300} color={C.success} dur="1.8s" /></div>
      <div style={{ fontSize: 20, color: '#8a929c', marginTop: 20 }}>시스템 점검 중 · 잠시 기다리세요. 곧 시작됩니다.</div>
    </Light>
  )
}

/* ================= 02 · 점검 완료 (X_WHEELS) =================
   바퀴는 평소 내려가 있고, 안전핀을 뽑으면 올라간다(10/5 팀장 확인). 바퀴를 내리는 단계는 없다.
   노드 X_WHEELS 삭제는 protocol/ 합의 대기(DECISIONS D-42). 그때까지 점검 완료만 잠깐 보여 준다 */
export function Screen02() {
  return (
    <Light>
      <Header title="점검 완료" right="2 / 8" />
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 26 }}>
        <div style={{ width: 600, height: 320, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          <img src={IMG.device} alt="SAVER 장치" style={{ maxWidth: '100%', maxHeight: '100%', objectFit: 'contain', display: 'block' }} />
        </div>
        <div style={{ fontSize: 40, fontWeight: 800 }}>점검을 마쳤습니다</div>
        <Dot color={C.success} label="곧 다음 화면으로 넘어갑니다" />
      </div>
    </Light>
  )
}

/* ================= 03 · 환자에게 이동 =================
   사이렌은 이 화면에서 울리지 않는다(구조자가 장치 옆에서 119와 통화 중이다). 사이렌은 KSME 결선에 달지 않는다 */
export function Screen03({ fire }) {
  return (
    <Light>
      <Header title="환자에게 이동" right="3 / 8" />
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 26 }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 18, width: '100%' }}>
          <div style={{ width: 420, height: 300, display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0 }}>
            <img src={IMG.device} alt="SAVER 장치" style={{ maxWidth: '100%', maxHeight: '100%', objectFit: 'contain', display: 'block' }} />
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 9, flexShrink: 0, animation: 'arrowmove 1.3s ease-in-out infinite' }}>
            <div style={{ width: 16, height: 16, borderRadius: '50%', background: C.success, opacity: .3 }} />
            <div style={{ width: 16, height: 16, borderRadius: '50%', background: C.success, opacity: .55 }} />
            <div style={{ width: 16, height: 16, borderRadius: '50%', background: C.success, opacity: .8 }} />
            <div style={{ width: 0, height: 0, borderTop: '15px solid transparent', borderBottom: '15px solid transparent', borderLeft: `26px solid ${C.success}` }} />
          </div>
          <div style={{ width: 330, height: 240, border: `1px solid ${C.lightLine}`, borderRadius: 10, overflow: 'hidden', background: '#0e1116', flexShrink: 0 }}>
            <img src={IMG.patient_angled} alt="환자" style={{ width: '100%', height: '100%', objectFit: 'cover', display: 'block' }} />
          </div>
        </div>
        <div style={{ fontSize: 26, fontWeight: 600 }}>환자 위치로 끌고 이동해 주세요</div>
        <Btn variant="success" w={340} h={74} fs={26} onPress={() => fire(EV.MOVE_DONE)}>이동 완료</Btn>
      </div>
    </Light>
  )
}

/* 헤드셋 그림 (머리띠와 귀덮개) */
function HeadsetIcon({ size = 34, color = C.primary }) {
  return (
    <svg width={size} height={size} viewBox="0 0 32 32" aria-hidden="true">
      <path d="M5 19 V15 a11 11 0 0 1 22 0 V19" fill="none" stroke={color} strokeWidth="2.6" strokeLinecap="round" />
      <rect x="3" y="17" width="7" height="11" rx="2.5" fill={color} />
      <rect x="22" y="17" width="7" height="11" rx="2.5" fill={color} />
    </svg>
  )
}

/* 119 신고 그림 (휴대전화) */
function PhoneIcon({ size = 150 }) {
  return (
    <svg width={size * 0.62} height={size} viewBox="0 0 62 100" aria-hidden="true">
      <rect x="3" y="3" width="56" height="94" rx="9" fill="#1a1f29" />
      <rect x="8" y="13" width="46" height="70" rx="3" fill="#fff" />
      <text x="31" y="56" textAnchor="middle" fontSize="20" fontWeight="800" fill={C.danger} fontFamily="sans-serif">119</text>
      <circle cx="31" cy="90" r="3" fill="#5b6472" />
    </svg>
  )
}

/* 한 단계 화면: 그림, 지시 한 줄(큰 글씨), 설명(작게, 지시 아님) */
function StepBody({ pic, title, sub, titleColor = '#1a1f29' }) {
  return (
    <div style={{ position: 'absolute', inset: '0 0 96px', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 18 }}>
      <div style={{ height: 210, display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 40 }}>{pic}</div>
      <div style={{ fontSize: 46, fontWeight: 800, color: titleColor, textAlign: 'center', lineHeight: 1.25 }}>{title}</div>
      {sub && <div style={{ fontSize: 22, color: '#3a4250', textAlign: 'center' }}>{sub}</div>}
    </div>
  )
}

/* ================= 04 · 시작 대기 · S2 · UI-02 =================
   한 화면에 지시 하나(10/5): ① 119 신고와 자동심장충격기 요청 → ② 헤드셋 → ③ 단단한 바닥 → [시작].
   119 신고와 자동심장충격기 요청이 장치 사용보다 먼저다(119 현장경험자 인터뷰 권고, v2.2).
   블로워 소리가 커서 음성 안내는 헤드셋으로 듣는다(D-40). 음성 V36(진입) → V40(step:2) → V04(step:3) */
export function Screen04({ fire }) {
  const [i, go] = useSteps(3)
  const body = [
    <StepBody key="call" pic={<><PhoneIcon /><AedMark size={130} /></>}
      title={<>먼저 <span style={{ color: C.danger }}>119</span>에 신고하고<br />자동심장충격기를 가져다 달라고 하세요</>} />,
    <div key="headset" data-headset style={{ position: 'absolute', inset: 0 }}>
      <StepBody pic={<HeadsetIcon size={170} />} title="헤드셋을 착용해 주세요" sub="장치 소리가 커서, 음성 안내는 헤드셋으로 들립니다" />
    </div>,
    <StepBody key="floor" pic={
      <div style={{ width: 540, height: 190, border: `1px solid ${C.lightLine}`, borderRadius: 10, overflow: 'hidden', background: '#0e1116' }}>
        <img src={IMG.patient_angled} alt="환자를 바닥에 눕힌 모습" style={{ width: '100%', height: '100%', objectFit: 'cover', display: 'block' }} />
      </div>} title="환자를 단단한 바닥에 눕히세요" />,
  ][i]
  return (
    <Light band={C.success}>
      <Header title="시작 대기" right="4 / 8" />
      <div style={{ flex: 1, position: 'relative' }}>
        {body}
        {i > 0 && <Btn variant="ghost" w={160} fs={23} onPress={() => go(i - 1)}
          style={{ position: 'absolute', left: 28, bottom: 24 }}>이전</Btn>}
        <div style={{ position: 'absolute', left: 0, right: 0, bottom: 44, display: 'flex', justifyContent: 'center', pointerEvents: 'none' }}>
          <StepDots n={3} i={i} />
        </div>
        <Btn w={240} fs={24} onPress={() => (i < 2 ? go(i + 1) : fire(EV.START_PRESSED))}
          style={{ position: 'absolute', right: 28, bottom: 24 }}>{i < 2 ? '다음' : '시작'}</Btn>
      </div>
    </Light>
  )
}

/* ================= 05 · 겉옷 제거 =================
   겉옷 제거는 인식하지 않는다(D-38). 제거하라고 안내만 하고,
   구조자가 [겉옷을 벗겼습니다]를 누르면 다음 화면으로 간다.
   그림은 벗기기 전과 뒤를 나란히 보여 주는 안내용이다. */
export function Screen05({ fire }) {
  const pic = (src, alt, label) => (
    <div style={{ flex: 1, minWidth: 0, display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 8 }}>
      <div style={{ width: '100%', flex: 1, minHeight: 0, borderRadius: 12, overflow: 'hidden', background: '#0e1116', border: '2px solid #d7dce2' }}>
        <img src={src} alt={alt} style={{ width: '100%', height: '100%', objectFit: 'cover', objectPosition: 'center', display: 'block' }} />
      </div>
      <div style={{ fontSize: 19, color: '#3a4250', fontWeight: 600 }}>{label}</div>
    </div>
  )
  return (
    <Light>
      <Header title="겉옷 정리 단계" right="5 / 8" />
      <div style={{ flex: 1, padding: '16px 28px 18px', display: 'flex', flexDirection: 'column', minHeight: 0 }}>
        <div style={{ fontSize: 26, fontWeight: 700, textAlign: 'center', marginBottom: 12, flexShrink: 0 }}>
          가슴 압박을 할 수 있도록 겉옷을 벗겨 주세요
        </div>
        <div style={{ flex: 1, minHeight: 0, display: 'flex', alignItems: 'stretch', gap: 18 }}>
          {pic(IMG.clothes_on, '겉옷을 입은 환자 상체', '지금')}
          <div style={{ alignSelf: 'center', fontSize: 40, color: C.success, fontWeight: 800, flexShrink: 0 }}>→</div>
          {pic(IMG.clothes_off, '겉옷을 벗긴 환자 상체', '벗긴 뒤')}
        </div>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginTop: 14, flexShrink: 0 }}>
          <Btn variant="ghost" w={160} fs={23} onPress={() => fire(EV.CANCEL)}>취소</Btn>
          <Btn variant="success" w={300} h={70} fs={26} fw={700}
            onPress={() => fire(EV.CLOTHES_OK)}>겉옷을 벗겼습니다</Btn>
        </div>
      </div>
    </Light>
  )
}

/* ================= 06 · 몸 위치 맞추기 (흉곽 정렬) · S3·S4 · UI-03/04 =================
   한 화면에 지시 하나(10/5): ① 팔과 다리를 몸에 붙이기 → ② 초록색 선에 맞추기.
   음성 V08(진입) → V07(step:2). 위치 오류면 ②에서 시작하고 V09 */
export function Screen06({ fire, demo }) {
  const err = demo.positionError
  const [step, go] = useSteps(2, err ? 1 : 0)
  const i = err ? 1 : step
  const lines = i === 1
  return (
    <Light band={err ? C.caution : null} bandH={12}>
      <Header title="몸 위치 맞추기" right={err ? '주의' : '6 / 8'} rightColor={err ? '#9a7000' : null} />
      <div style={{ flex: 1, position: 'relative', display: 'flex', flexDirection: 'column', alignItems: 'center', padding: '22px 28px' }}>
        <div style={{ position: 'relative', width: 500, height: 250, border: '1px solid #c4cbd4', borderRadius: 10, overflow: 'hidden', background: '#0e1116' }}>
          <img src={IMG.patient_topdown} alt="환자 상체 (위에서 본 모습)" style={{ width: '100%', height: '100%', objectFit: 'cover', objectPosition: 'center', display: 'block' }} />
          {lines && <>
            <div style={{ position: 'absolute', left: '31%', top: '6%', bottom: '6%', width: 3, background: C.success, transform: 'translateX(-50%)', boxShadow: `0 0 6px ${C.success}` }} />
            <div style={{ position: 'absolute', top: '50%', left: '6%', right: '6%', height: 3, background: C.success, boxShadow: `0 0 6px ${C.success}` }} />
            <div style={{ position: 'absolute', left: '31%', top: '50%', width: 24, height: 24, border: `3px solid ${C.success}`, borderRadius: '50%', background: 'rgba(30,142,62,.15)', transform: 'translate(-50%,-50%)', boxShadow: '0 0 8px rgba(30,142,62,.6)' }} />
          </>}
        </div>
        <div style={{ fontSize: 40, fontWeight: 800, marginTop: 20 }}>
          {err ? '몸 위치를 확인하세요' : lines ? '초록색 선에 환자 몸을 맞춰 주세요' : '팔과 다리를 몸에 붙여 주세요'}
        </div>
        <div style={{ fontSize: 21, color: '#3a4250', marginTop: 8, textAlign: 'center' }}>
          {lines ? '긴 선이 몸 한가운데에 오게 합니다. 환자를 들 필요는 없습니다.' : '장치 바퀴가 몸 옆으로 지나갑니다.'}
        </div>
        <Btn variant="ghost" w={160} fs={23} onPress={() => (i === 1 && !err ? go(0) : fire(EV.CANCEL))}
          style={{ position: 'absolute', left: 28, bottom: 24 }}>{i === 1 && !err ? '이전' : '취소'}</Btn>
        {!err && <div style={{ position: 'absolute', left: 0, right: 0, bottom: 44, display: 'flex', justifyContent: 'center', pointerEvents: 'none' }}>
          <StepDots n={2} i={i} />
        </div>}
        <Btn variant={err ? 'warning' : 'success'} w={err ? 300 : 280} fs={26} fw={700}
          onPress={() => (err ? fire(EV.POSITION_ERROR) : i === 0 ? go(1) : fire(EV.POSITION_OK))}
          style={{ position: 'absolute', right: 28, bottom: 24 }}>
          {err ? '위치 오류 · 확인' : i === 0 ? '다음' : '맞췄습니다'}
        </Btn>
      </div>
    </Light>
  )
}

/* ================= 07 · 손가락 센서 (PPG) 끼우기 ================= */
export function Screen07({ fire }) {
  return (
    <Light>
      <Header title="손가락 센서 끼우기" right="7 / 8" />
      <div style={{ flex: 1, position: 'relative', display: 'flex', alignItems: 'center', gap: 30, padding: '0 40px' }}>
        <div style={{ width: 360, height: 300, background: '#fff', border: `1px solid ${C.lightLine}`, borderRadius: 10, overflow: 'hidden', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          <img src={IMG.ppg_sensor} alt="손가락 센서" style={{ width: '100%', height: '100%', objectFit: 'contain', display: 'block' }} />
        </div>
        <div style={{ flex: 1 }}>
          <div style={{ fontSize: 40, fontWeight: 700, lineHeight: 1.3 }}>손가락 센서를<br />환자의 손가락에<br />끼워 주세요</div>
          <div style={{ marginTop: 28 }}><Dot color={C.success} label="자동 인식 중…" size={14} fontSize={24} /></div>
        </div>
        <Btn w={240} fs={24} onPress={() => fire(EV.PPG_ATTACHED)}
          style={{ position: 'absolute', right: 28, bottom: 24 }}>다음</Btn>
      </div>
    </Light>
  )
}

/* ================= 08 · 바닥 고정 (음압 흡착) 확인 · S5 · UI-05 ================= */
export function Screen08({ fire, demo }) {
  const err = demo.vacuumError
  return (
    <Light band={err ? C.caution : null} bandH={12}>
      <Header title="바닥 고정 확인" right={err ? '주의 · 고정 약함' : '확인 중'} rightColor={err ? '#9a7000' : null} />
      <div style={{ flex: 1, position: 'relative', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 18 }}>
        <div style={{ width: 560, height: 250, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          <img src={IMG.device} alt="SAVER 장치" style={{ maxWidth: '100%', maxHeight: '100%', objectFit: 'contain', display: 'block' }} />
        </div>
        <div style={{ fontSize: 44, fontWeight: 800 }}>그대로 두세요</div>
        <div style={{ fontSize: 22, color: '#3a4250' }}>장치를 바닥에 고정하고 있습니다.</div>
        <Progress w={320} color={err ? C.caution : C.primary} dur="1.6s" />
        <div style={{ display: 'flex', gap: 12, marginTop: 6 }}>
          <Btn variant="ghostBlue" w={180} fs={23} onPress={() => fire(err ? EV.VACUUM_ERROR : EV.VACUUM_OK)}>재확인</Btn>
          <Btn variant="ghost" w={160} fs={23} onPress={() => fire(EV.CANCEL)}>취소</Btn>
        </div>
      </div>
    </Light>
  )
}

/* 09 · 압박점 탐색 · X_SEARCH → screens/Search.jsx */


/* ================= 20 · 안전핀 뽑기 (X_PIN) =================
   탐색으로 압박점 위에 선 뒤, 구조자가 안전핀을 뽑는다. 바퀴는 평소 내려가 있고 핀을 뽑으면
   올라가 패드가 바닥에 닿는다(10/5 팀장 확인. 구조자가 바퀴를 올리지 않는다).
   한 화면에 지시 하나(10/5): ① 안전핀 → ② 헤드셋 확인(다음 화면부터 블로워 소리, D-40).
   음성 V14(진입) → V41(step:2). 다음 화면(S5)에 들어가면 음압을 켠다. */
export function Screen20({ fire }) {
  const [i, go] = useSteps(2)
  return (
    <Light>
      <Header title="안전핀 뽑기" right="위치 찾음" />
      <div style={{ flex: 1, position: 'relative' }}>
        {i === 0
          ? <div style={{ position: 'absolute', inset: '0 0 96px', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 14 }}>
              <div style={{ width: 460, height: 230, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
                <img src={IMG.device} alt="SAVER 장치" style={{ maxWidth: '100%', maxHeight: '100%', objectFit: 'contain', display: 'block' }} />
              </div>
              <Dot color={C.success} label="압박 위치를 찾았습니다" />
              <div style={{ fontSize: 46, fontWeight: 800 }}>안전핀을 뽑으세요</div>
              <div style={{ fontSize: 22, color: '#3a4250' }}>핀을 뽑으면 바퀴가 올라가고 패드가 바닥에 닿습니다.</div>
            </div>
          : <div data-headset style={{ position: 'absolute', inset: '0 0 96px', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 18 }}>
              <HeadsetIcon size={170} />
              <div style={{ fontSize: 46, fontWeight: 800 }}>헤드셋을 썼는지 확인하세요</div>
              <div style={{ fontSize: 22, color: '#3a4250' }}>다음 화면부터 장치 소리가 커집니다.</div>
            </div>}
        <Btn variant="ghost" w={160} fs={23} onPress={() => (i === 0 ? fire(EV.CANCEL) : go(0))}
          style={{ position: 'absolute', left: 28, bottom: 24 }}>{i === 0 ? '취소' : '이전'}</Btn>
        <div style={{ position: 'absolute', left: 0, right: 0, bottom: 44, display: 'flex', justifyContent: 'center', pointerEvents: 'none' }}>
          <StepDots n={2} i={i} />
        </div>
        <Btn w={260} fs={24} onPress={() => (i === 0 ? go(1) : fire(EV.PIN_RELEASED))}
          style={{ position: 'absolute', right: 28, bottom: 24 }}>{i === 0 ? '핀을 뽑았습니다' : '확인했습니다'}</Btn>
      </div>
    </Light>
  )
}

/* ================= 10 · 압박 준비 / 손 떼기 · S6 · UI-06 ================= */
export function Screen10({ fire, ctx }) {
  return (
    <Light>
      <Header title="압박 준비" right="최종 안전 확인" />
      <PressArea onPress={() => fire(EV.CPR_START)}
        style={{ flex: 1, position: 'relative', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 18, animation: 'bluepulse 1.6s ease-in-out infinite' }}>
        <div style={{ position: 'absolute', inset: 14, border: `2px dashed ${C.primary}`, borderRadius: 12, opacity: .4, pointerEvents: 'none', animation: 'sblink 1.6s ease-in-out infinite' }} />
        {ctx && typeof ctx.aedPads === 'boolean' && (
          <div style={{ position: 'absolute', top: 28, right: 32, fontSize: 18, fontWeight: 700, pointerEvents: 'none',
            color: ctx.aedPads ? C.success : '#6b7280' }}>
            자동심장충격기 패드: {ctx.aedPads ? '붙임' : '아직 없음'}
          </div>
        )}
        <div style={{ width: 84, height: 84, borderRadius: '50%', border: `6px solid ${C.danger}`, position: 'relative' }}>
          <div style={{ position: 'absolute', top: '50%', left: '8%', right: '8%', height: 6, background: C.danger, transform: 'translateY(-50%) rotate(45deg)' }} />
        </div>
        {/* 지시는 손 떼기 하나. 화면 전체가 시작 버튼이다(10/5 한 화면에 지시 하나) */}
        <div style={{ fontSize: 48, fontWeight: 800 }}>환자에게서 손을 떼세요</div>
        <div style={{ marginTop: 2 }}><Dot color={C.primary} label="화면을 누르면 압박이 시작됩니다" size={14} fontSize={24} /></div>
        <Btn variant="ghost" w={160} fs={23} onPress={() => fire(EV.CANCEL)}
          style={{ position: 'absolute', left: 28, bottom: 24 }}>취소</Btn>
      </PressArea>
    </Light>
  )
}
