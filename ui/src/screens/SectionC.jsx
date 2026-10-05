import React from 'react'
import { C } from '../tokens'
import { EV } from '../stateMachine'
import { Dark, AlarmRed, Header, Btn, WavePanel } from '../ui'

import { IMG } from '../images'
import { reasonText } from '../search'

/* ================= 13 · ROSC 의심 · S9 · UI-09 · ★핵심·경고 ================= */
export function Screen13({ fire, waves, paused = true }) {
  return (
    <Dark band={C.warning} bandH={14}>
      <Header dark h={72} titleSize={25} title="회복 신호 확인" titleEn="Recovery check"
        right="경고 · WARNING" rightColor={C.warning} />
      <div style={{ flex: 1, display: 'flex', minHeight: 0 }}>
        <div style={{ width: '46%', background: 'linear-gradient(180deg,rgba(242,96,0,.22),rgba(242,96,0,.07))', padding: '24px 26px', display: 'flex', flexDirection: 'column' }}>
          <div style={{ flex: 1, display: 'flex', flexDirection: 'column', justifyContent: 'center', alignItems: 'center', textAlign: 'center' }}>
            {paused ? (
              <>
                <div style={{ fontSize: 46, fontWeight: 800, color: '#FF8A3D', lineHeight: 1.25 }}>환자 상태를<br />확인하세요</div>
                <div style={{ fontSize: 25, color: '#eef1f6', marginTop: 18, lineHeight: 1.5 }}>
                  심장이 다시 뛰는 것 같습니다.<br />숨쉬기와 맥박을 확인하세요.
                </div>
              </>
            ) : (
              /* 장치가 멈춤을 보고하기 전에는 손을 대게 하지 않는다(v2.3) */
              <div data-pause-state="pausing" style={{ fontSize: 40, fontWeight: 800, color: '#FF8A3D', lineHeight: 1.3 }}>압박을 멈추는 중입니다<br />잠시 기다리세요</div>
            )}
          </div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
            <div style={{ display: 'flex', gap: 10 }}>
              <Btn flex={1} fs={22} fw={700} disabled={!paused} onPress={() => fire(EV.RESUME_CPR)}>압박 재개</Btn>
              <Btn flex={1} fs={22} variant="warning" onPress={() => fire(EV.STOP_CPR)}>압박 중단</Btn>
            </div>
            {/* 장치는 ROSC를 확정하지 않는다 — 사용자 확인 기록용 (연동표 v2 §0) */}
            <Btn variant="ghostDark" h={52} fs={20} fw={400} onPress={() => fire(EV.STOP_CPR)}>회복을 확인했습니다</Btn>
          </div>
        </div>
        <div style={{ flex: 1, padding: 16, display: 'flex', flexDirection: 'column', gap: 10, minHeight: 0 }}>
          <WavePanel label="숨쉬기 (레이더 추정) / Breathing" d={waves.breath} color={C.breath} dur="11s" pad="8px 12px" labelSize={14} />
          <WavePanel label="손가락 센서 신호 / Finger sensor" d={waves.ppg2} color={C.ppg} dur="6s" pad="8px 12px" labelSize={14} />
        </div>
      </div>
    </Dark>
  )
}

/* ================= 14 · 기기 오류 확인 · S10 · UI-10 · 경고 ================= */
const CAUSES = {
  // 음압은 장치를 바닥에 붙인다(가슴이 아니다). 화면에는 '바닥 고정'으로 쓴다
  tubing:   [{ t: '호스 연결을 확인하세요',   s: '→ 호스를 다시 끼운 뒤 재확인을 누르세요.' },
             { t: '바닥 고정이 약합니다',     s: '→ 패드가 바닥에 닿아 있는지 확인하세요.' }],
  vacuum:   [{ t: '바닥 고정이 약합니다',     s: '→ 패드가 바닥에 닿아 있는지 확인하세요.' },
             { t: '바닥에 붙는 힘이 유지되지 않습니다', s: '→ 패드와 바닥면 상태를 확인하세요.' }],
  position: [{ t: '압박 위치를 확인하세요',   s: '→ 초록색 선에 환자 몸을 맞춰 주세요.' },
             { t: '장치가 가슴 가운데에서 벗어났습니다', s: '→ 장치를 들어 다시 놓으세요.' }],
  sensor:   [{ t: '센서 신호가 불안정합니다', s: '→ 손가락 센서가 잘 끼워져 있는지 확인하세요.' }],
  search:   [{ t: '압박 위치 찾기가 중단되었습니다', s: '→ 환자 몸 위치를 확인한 뒤 재확인을 누르면 찾기 화면으로 돌아갑니다.' }],
}

export function Screen14({ fire, ctx, search }) {
  let list = CAUSES[ctx.errorCause] || CAUSES.tubing
  const why = ctx.errorCause === 'search' && search ? reasonText(search.reason) : null
  if (why) list = [...list, { t: why.title, s: '→ ' + why.sub + ' (' + why.code + ')' }]
  return (
    <Dark band={C.warning} bandH={14}>
      <Header dark h={72} titleSize={26} title="장치 상태 확인" right="경고" rightColor={C.warning} />
      <div style={{ flex: 1, position: 'relative', padding: '24px 30px', display: 'flex', flexDirection: 'column' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 16 }}>
          <div style={{ width: 56, height: 56, borderRadius: '50%', background: C.warning, color: '#fff', display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 34, fontWeight: 800 }}>!</div>
          <div style={{ fontSize: 36, fontWeight: 800 }}>장치 상태를 확인하세요</div>
        </div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 14, marginTop: 26 }}>
          {list.map((r, i) => (
            <div key={i} style={{ background: C.darkCard, borderRadius: 10, padding: '16px 18px', display: 'flex', alignItems: 'center', gap: 14 }}>
              <span style={{ width: 12, height: 12, borderRadius: '50%', background: C.warning, flexShrink: 0 }} />
              <div>
                <div style={{ fontSize: 22, fontWeight: 700 }}>{r.t}</div>
                <div style={{ fontSize: 17, color: '#9aa3b0', marginTop: 3 }}>{r.s}</div>
              </div>
            </div>
          ))}
        </div>
        <div style={{ position: 'absolute', right: 30, bottom: 24, display: 'flex', gap: 12 }}>
          <Btn w={200} fs={23} fw={700} onPress={() => fire(EV.RECHECK)}>재확인</Btn>
          <Btn variant="ghostDark" w={160} fs={23} fw={400} onPress={() => fire(EV.STOP)}>정지</Btn>
        </div>
      </div>
    </Dark>
  )
}

/* ================= 15 · 압박 위치 이탈 경고 · 경보음 ================= */
export function Screen15({ fire, paused = true }) {
  return (
    <AlarmRed>
      <div style={{ height: 72, display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '0 26px', borderBottom: '1px solid rgba(255,255,255,.35)', flexShrink: 0 }}>
        <div style={{ fontSize: 25, fontWeight: 800 }}>
          압박 위치 확인 <span style={{ fontSize: 17, fontWeight: 500, color: 'rgba(255,255,255,.8)' }}>/ Position Alert</span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, fontSize: 18, fontWeight: 700 }}>
          <span data-safety-blink style={{ width: 11, height: 11, borderRadius: '50%', background: '#fff', display: 'inline-block', animation: 'sblink .7s infinite' }} />경보음
        </div>
      </div>
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 14, padding: '0 30px' }}>
        <div style={{ fontSize: 40, fontWeight: 800, textAlign: 'center' }}>⚠ 압박 위치가 어긋났습니다</div>
        {/* v2.3: 위치를 다시 맞춘 뒤 재개하는 길(REALIGN_DONE)은 복구 절차가 정해질 때까지 없다. 지시는 [정지] 하나 */}
        <div style={{ fontSize: 26, fontWeight: 700, color: 'rgba(255,255,255,.95)', textAlign: 'center' }}>
          {paused ? '압박을 멈췄습니다. [정지]를 누르세요' : '압박을 멈추는 중입니다. [정지]를 누르세요'}<br />
          <span style={{ fontSize: 17, fontWeight: 400, color: 'rgba(255,255,255,.7)' }}>Compression paused. Press Stop.</span>
        </div>
        <div style={{ position: 'relative', width: 420, height: 200, border: '1px solid #c4cbd4', borderRadius: 10, overflow: 'hidden', background: '#0e1116' }}>
          <img src={IMG.patient_topdown} alt="환자 (위에서 본 모습)" style={{ width: '100%', height: '100%', objectFit: 'cover', objectPosition: 'center', display: 'block' }} />
          <div style={{ position: 'absolute', left: '31%', top: '6%', bottom: '6%', width: 3, background: C.success, transform: 'translateX(-50%)', boxShadow: `0 0 6px ${C.success}` }} />
          <div style={{ position: 'absolute', top: '50%', left: '6%', right: '6%', height: 3, background: C.success, boxShadow: `0 0 6px ${C.success}` }} />
          <div style={{ position: 'absolute', left: '31%', top: '50%', width: 24, height: 24, border: `3px solid ${C.success}`, borderRadius: '50%', background: 'rgba(30,142,62,.15)', transform: 'translate(-50%,-50%)', boxShadow: '0 0 8px rgba(30,142,62,.6)' }} />
          <div data-safety-blink style={{ position: 'absolute', left: '52%', top: '30%', width: 26, height: 26, borderRadius: '50%', background: C.danger, boxShadow: '0 0 0 6px rgba(216,40,40,.25)', animation: 'sblink .8s infinite' }} />
          <div style={{ position: 'absolute', left: '55%', top: '15%', font: "600 12px 'Pretendard',sans-serif", color: '#ff8a8a' }}>현재 위치</div>
        </div>
        <div style={{ display: 'flex', gap: 14, marginTop: 4 }}>
          <Btn variant="white" w={260} h={70} fs={28} onPress={() => fire(EV.STOP)}>정지</Btn>
        </div>
      </div>
    </AlarmRed>
  )
}

/* ================= 16 · 안전정지 · S11 · UI-11 · ★핵심·위험 ================= */
export function Screen16({ fire }) {
  return (
    <AlarmRed>
      <div style={{ height: 80, display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '0 28px', borderBottom: '1px solid rgba(255,255,255,.35)', flexShrink: 0 }}>
        <div style={{ fontSize: 28, fontWeight: 800 }}>안전 정지 · SAFETY STOP</div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, fontSize: 19, fontWeight: 700 }}>
          <span data-safety-blink style={{ width: 12, height: 12, borderRadius: '50%', background: '#fff', display: 'inline-block', animation: 'sblink .6s infinite' }} />연속 경고음
        </div>
      </div>
      <div style={{ flex: 1, position: 'relative', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', padding: '0 40px' }}>
        <div style={{ fontSize: 58, fontWeight: 800, textAlign: 'center', lineHeight: 1.2 }}>장치를 멈췄습니다<br />환자를 확인하세요</div>
        <div style={{ fontSize: 24, marginTop: 16, opacity: .95 }}>수동으로 장치를 해제할 수 있습니다.</div>
        <div style={{ position: 'absolute', left: 32, right: 32, bottom: 28, display: 'flex', gap: 18 }}>
          <Btn variant="white" flex={1} h={90} fs={30} onPress={() => fire(EV.MANUAL_RELEASE)}>수동 해제</Btn>
          <Btn variant="outlineWhite" flex={1} h={90} fs={30} onPress={() => fire(EV.END)}>종료</Btn>
        </div>
      </div>
    </AlarmRed>
  )
}
