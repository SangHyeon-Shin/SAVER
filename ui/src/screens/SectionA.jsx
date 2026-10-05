import React from 'react'
import { C } from '../tokens'
import { EV } from '../stateMachine'
import { Light, Header, Btn, Dot, Progress, PressArea } from '../ui'

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

/* ================= 02 · 바퀴 내리기 · 이동 준비 ================= */
export function Screen02() {
  return (
    <Light>
      <Header title="이동 준비 단계" right="2 / 8" />
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 26 }}>
        <div style={{ width: 660, height: 340, position: 'relative', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          <img src={IMG.device} alt="SAVER 장치" style={{ maxWidth: '100%', maxHeight: '100%', objectFit: 'contain', display: 'block' }} />
          <div style={{ position: 'absolute', left: '18%', bottom: 12, color: C.success, fontSize: 26, fontWeight: 800, animation: 'pulsedot 1.3s infinite' }}>↓</div>
          <div style={{ position: 'absolute', right: '18%', bottom: 12, color: C.success, fontSize: 26, fontWeight: 800, animation: 'pulsedot 1.3s infinite' }}>↓</div>
        </div>
        <div style={{ fontSize: 24 }}>이동을 위해 바퀴를 내리고 있습니다.</div>
        <Dot color={C.success} label="자동 진행 중" />
      </div>
    </Light>
  )
}

/* ================= 03 · 환자에게 이동 · 사이렌 ================= */
export function Screen03({ fire }) {
  return (
    <Light>
      <div data-safety-blink style={{ position: 'absolute', inset: 0, pointerEvents: 'none', animation: 'sirenglow 1s infinite', zIndex: 5 }} />
      <Header title="환자에게 이동" right={<Dot color={C.danger} label="사이렌 작동" fast />} />
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

/* ================= 04 · 시작 대기 · S2 · UI-02 ================= */
export function Screen04({ fire }) {
  return (
    <Light band={C.success}>
      <Header title="시작 대기" right="4 / 8" />
      <div style={{ flex: 1, position: 'relative' }}>
        <div style={{ position: 'absolute', inset: 0, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 26 }}>
          <div style={{ width: 540, height: 230, border: `1px solid ${C.lightLine}`, borderRadius: 10, overflow: 'hidden', background: '#0e1116' }}>
            <img src={IMG.patient_angled} alt="환자를 바닥에 눕힌 모습" style={{ width: '100%', height: '100%', objectFit: 'cover', display: 'block' }} />
          </div>
          <div style={{ fontSize: 48, fontWeight: 800, color: '#1a1f29' }}>환자를 단단한 바닥에 눕히세요.</div>
          {/* 119 신고와 AED 요청이 장치 사용보다 먼저다(119 현장경험자 인터뷰 권고, v2.2) */}
          <div style={{ fontSize: 26, fontWeight: 700, color: C.danger }}>119 신고와 AED 요청을 먼저 하세요.</div>
        </div>
        <Btn w={240} fs={24} onPress={() => fire(EV.START_PRESSED)}
          style={{ position: 'absolute', right: 28, bottom: 24 }}>시작</Btn>
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
          CPR을 수행할 수 있도록 겉옷을 제거해 주세요
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

/* ================= 06 · 흉곽 정렬 / 유도선 · S3·S4 · UI-03/04 ================= */
export function Screen06({ fire, demo }) {
  const err = demo.positionError
  return (
    <Light band={err ? C.caution : null} bandH={12}>
      <Header title="흉곽 정렬 단계" right={err ? '주의' : '6 / 8'} rightColor={err ? '#9a7000' : null} />
      <div style={{ flex: 1, position: 'relative', display: 'flex', flexDirection: 'column', alignItems: 'center', padding: '22px 28px' }}>
        <div style={{ position: 'relative', width: 460, height: 230, border: '1px solid #c4cbd4', borderRadius: 10, overflow: 'hidden', background: '#0e1116' }}>
          <img src={IMG.patient_topdown} alt="환자 상체 (위에서 본 모습)" style={{ width: '100%', height: '100%', objectFit: 'cover', objectPosition: 'center', display: 'block' }} />
          <div style={{ position: 'absolute', left: '31%', top: '6%', bottom: '6%', width: 3, background: C.success, transform: 'translateX(-50%)', boxShadow: `0 0 6px ${C.success}` }} />
          <div style={{ position: 'absolute', top: '50%', left: '6%', right: '6%', height: 3, background: C.success, boxShadow: `0 0 6px ${C.success}` }} />
          <div style={{ position: 'absolute', left: '31%', top: '50%', width: 24, height: 24, border: `3px solid ${C.success}`, borderRadius: '50%', background: 'rgba(30,142,62,.15)', transform: 'translate(-50%,-50%)', boxShadow: '0 0 8px rgba(30,142,62,.6)' }} />
        </div>
        <div style={{ fontSize: 26, fontWeight: 700, marginTop: 18 }}>
          {err ? '정렬 위치를 확인하세요' : '초록색 유도선을 따라 환자를 정렬해 주세요'}
        </div>
        <div style={{ fontSize: 20, color: '#3a4250', marginTop: 8, textAlign: 'center' }}>
          긴 유도선이 환자의 정중앙에 오도록 맞추세요. 환자를 들 필요는 없습니다.<br />
          바퀴가 지나가도록 팔과 다리를 몸에 붙여 차렷 자세로 해 주세요.
        </div>
        <div style={{ display: 'flex', gap: 12, marginTop: 22 }}>
          <Btn variant="ghost" w={150} h={80} fs={24} onPress={() => fire(EV.CANCEL)}>취소</Btn>
          <Btn variant={err ? 'warning' : 'success'} w={440} h={80} fs={30} fw={700}
            onPress={() => fire(err ? EV.POSITION_ERROR : EV.POSITION_OK)}>
            {err ? '위치 오류 · 확인' : '정렬 완료'}
          </Btn>
        </div>
      </div>
    </Light>
  )
}

/* ================= 07 · PPG 센서 착용 ================= */
export function Screen07({ fire }) {
  return (
    <Light>
      <Header title="PPG 센서 착용" right="7 / 8" />
      <div style={{ flex: 1, position: 'relative', display: 'flex', alignItems: 'center', gap: 30, padding: '0 40px' }}>
        <div style={{ width: 360, height: 300, background: '#fff', border: `1px solid ${C.lightLine}`, borderRadius: 10, overflow: 'hidden', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          <img src={IMG.ppg_sensor} alt="PPG 센서" style={{ width: '100%', height: '100%', objectFit: 'contain', display: 'block' }} />
        </div>
        <div style={{ flex: 1 }}>
          <div style={{ fontSize: 40, fontWeight: 700, lineHeight: 1.3 }}>PPG 센서를<br />환자의 손가락에<br />착용시켜 주세요</div>
          <div style={{ marginTop: 28 }}><Dot color={C.success} label="자동 인식 중…" size={14} fontSize={24} /></div>
        </div>
        <Btn w={240} fs={24} onPress={() => fire(EV.PPG_ATTACHED)}
          style={{ position: 'absolute', right: 28, bottom: 24 }}>다음</Btn>
      </div>
    </Light>
  )
}

/* ================= 08 · 흡착·고정 확인 · S5 · UI-05 ================= */
export function Screen08({ fire, demo }) {
  const err = demo.vacuumError
  return (
    <Light band={err ? C.caution : null} bandH={12}>
      <Header title="흡착·고정 확인" right={err ? '주의 · 흡착 불안정' : '확인 중'} rightColor={err ? '#9a7000' : null} />
      <div style={{ flex: 1, position: 'relative', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 18 }}>
        <div style={{ width: 560, height: 250, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          <img src={IMG.device} alt="SAVER 장치" style={{ maxWidth: '100%', maxHeight: '100%', objectFit: 'contain', display: 'block' }} />
        </div>
        <div style={{ fontSize: 44, fontWeight: 800 }}>그대로 두세요</div>
        <div style={{ fontSize: 22, color: '#3a4250' }}>흡착·고정 상태를 확인하고 있습니다.</div>
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


/* ================= 20 · 바퀴 수납 · 안전핀 해제 (X_PIN) =================
   탐색으로 압박점 위에 선 뒤, 구조자가 안전핀을 빼 바퀴를 수납하고 패드를 바닥에 닿게 한다.
   다음 화면(S5)에 들어가면 음압을 켠다. */
export function Screen20({ fire }) {
  return (
    <Light>
      <Header title="바퀴 수납 · 안전핀 해제" right="탐색 완료" />
      <div style={{ flex: 1, position: 'relative', display: 'flex', alignItems: 'center', gap: 30, padding: '0 40px' }}>
        <div style={{ width: 360, height: 280, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          <img src={IMG.device} alt="SAVER 장치" style={{ maxWidth: '100%', maxHeight: '100%', objectFit: 'contain', display: 'block' }} />
        </div>
        <div style={{ flex: 1 }}>
          <div style={{ fontSize: 38, fontWeight: 700, lineHeight: 1.3 }}>압박점 위에 섰습니다.<br />안전핀을 뽑아<br />바퀴를 수납하세요</div>
          <div style={{ fontSize: 21, color: '#3a4250', marginTop: 16 }}>패드가 바닥에 닿으면 다음을 누르세요. 다음 화면에서 음압 고정을 시작합니다.</div>
        </div>
        <Btn variant="ghost" w={160} fs={23} onPress={() => fire(EV.CANCEL)}
          style={{ position: 'absolute', left: 28, bottom: 24 }}>취소</Btn>
        <Btn w={240} fs={24} onPress={() => fire(EV.PIN_RELEASED)}
          style={{ position: 'absolute', right: 28, bottom: 24 }}>핀을 뺐습니다</Btn>
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
            AED 패드: {ctx.aedPads ? '붙임' : '없음 · 도착하면 압박 화면에서 [AED 사용]'}
          </div>
        )}
        <div style={{ width: 84, height: 84, borderRadius: '50%', border: `6px solid ${C.danger}`, position: 'relative' }}>
          <div style={{ position: 'absolute', top: '50%', left: '8%', right: '8%', height: 6, background: C.danger, transform: 'translateY(-50%) rotate(45deg)' }} />
        </div>
        <div style={{ fontSize: 46, fontWeight: 800 }}>시작하려면 화면을 누르세요</div>
        <div style={{ fontSize: 26, fontWeight: 700, color: C.danger }}>환자에게서 모두 손을 떼세요.</div>
        <div style={{ marginTop: 2 }}><Dot color={C.primary} label="화면 아무 곳이나 누르면 압박이 시작됩니다" size={14} fontSize={21} /></div>
        <Btn variant="ghost" w={160} fs={23} onPress={() => fire(EV.CANCEL)}
          style={{ position: 'absolute', left: 28, bottom: 24 }}>취소</Btn>
      </PressArea>
    </Light>
  )
}
