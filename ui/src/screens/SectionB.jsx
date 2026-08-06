import React from 'react'
import { C } from '../tokens'
import { EV } from '../stateMachine'
import { Dark, Header, Btn, Wave, Stat, VitalTrendPanel } from '../ui'

/* ================= 11 · CPR 수행 모니터링 · S7 · UI-07 · ★핵심 ================= */
export function Screen11({ fire, vitals, mr60 }) {
  const sensorLive = mr60.connected && !mr60.stale && !!mr60.sample
  const sensorLabel = mr60.connecting ? '연결 중…' : sensorLive ? 'MR60 정상' : mr60.connected ? 'MR60 신호 대기' : 'MR60 연결'

  return (
    <Dark>
      <Header dark h={72} titleSize={26} title="압박 모니터링" titleEn="Compression Monitoring"
        right={
          <div style={{ display: 'flex', alignItems: 'center', gap: 9 }}>
            <span style={{ width: 11, height: 11, borderRadius: '50%', background: sensorLive ? C.success : C.caution, display: 'inline-block', animation: 'pulsedot 1.4s infinite' }} />
            <span style={{ fontSize: 17, color: sensorLive ? C.success : C.caution }}>{sensorLabel}</span>
            <button
              onPointerDown={(event) => { event.stopPropagation(); mr60.connected ? mr60.disconnect() : mr60.connect() }}
              disabled={mr60.connecting || !mr60.supported}
              style={{ height: 38, padding: '0 12px', borderRadius: 6, border: `1px solid ${sensorLive ? C.success : C.caution}`, background: 'transparent', color: sensorLive ? C.success : C.caution, fontSize: 14, fontWeight: 700 }}
            >
              {mr60.connected ? '해제' : '센서 연결'}
            </button>
          </div>
        } />

      {/* 안전 지시 밴드 */}
      <div style={{ height: 42, display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 10, background: 'rgba(216,40,40,.14)', color: '#ff8a8a', fontSize: 19, fontWeight: 700, flexShrink: 0 }}>
        압박 중 — 만지지 마세요&nbsp;<span style={{ color: '#c98' }}>/ Do not touch</span>
      </div>

      <div style={{ flex: 1, display: 'flex', gap: 14, padding: '14px 16px', minHeight: 0 }}>
        <div style={{ flex: 1.7, display: 'flex', flexDirection: 'column', gap: 12, minHeight: 0 }}>
          <VitalTrendPanel
            label="호흡 추이 / Respiration trend"
            value={mr60.sample?.breathRateBrpm}
            unit="회/분"
            history={mr60.history}
            field="breathRateBrpm"
            color={C.breath}
            min={0}
            max={40}
            status={mr60.sample?.breathStatus}
          />
          <VitalTrendPanel
            label="심박 추이 / Heart rate trend"
            value={mr60.sample?.heartRateBpm}
            unit="BPM"
            history={mr60.history}
            field="heartRateBpm"
            color={C.ppg}
            min={40}
            max={180}
            status={mr60.sample?.heartStatus}
          />
        </div>
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 10, minHeight: 0 }}>
          <Stat label="압박 깊이 / Depth" value={vitals.depth} />
          <Stat label="압박 속도 / Rate" value={vitals.rate} />
          <Stat label="압박 강도 / Force" value={vitals.force} />
          <div style={{ display: 'flex', gap: 10 }}>
            <Stat label="경과 시간" value={vitals.elapsed} big={false} />
            <Stat label="압박 횟수" value={vitals.count} big={false} />
          </div>
        </div>
      </div>

      {mr60.error && (
        <div style={{ position: 'absolute', left: 16, bottom: 76, maxWidth: 560, padding: '7px 12px', borderRadius: 6, background: 'rgba(216,40,40,.88)', color: '#fff', fontSize: 13 }}>
          {mr60.error}
        </div>
      )}

      <div style={{ height: 74, display: 'flex', alignItems: 'center', justifyContent: 'flex-end', gap: 12, padding: '0 16px', flexShrink: 0 }}>
        <Btn variant="ghostDark" w={190} h={58} fs={22} onPress={() => fire(EV.PAUSE)}>일시정지</Btn>
        <Btn variant="danger" w={190} h={58} fs={22} onPress={() => fire(EV.STOP)}>정지</Btn>
      </div>
    </Dark>
  )
}

/* ================= 12 · 일시정지 · S8 · UI-08 · 주의 ================= */
export function Screen12({ fire, waves }) {
  return (
    <Dark band={C.caution} bandH={12}>
      <Header dark h={72} titleSize={26} title="일시정지" titleEn="Paused" right="주의" rightColor={C.caution} />
      <div style={{ flex: 1, position: 'relative', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 14 }}>
        <div style={{ position: 'absolute', top: 24, left: 30, right: 30, bottom: 110, opacity: .22, overflow: 'hidden', borderRadius: 10 }}>
          <Wave d={waves.comp} color={C.wave} dur={null} double={false} />
        </div>
        <div style={{ position: 'relative', fontSize: 46, fontWeight: 800 }}>재개하려면 누르세요</div>
        <div style={{ position: 'relative', fontSize: 22, color: '#c5ccd6' }}>환자 상태를 확인하세요.</div>
        <div style={{ position: 'absolute', bottom: 24, display: 'flex', gap: 14 }}>
          <Btn w={230} fs={24} fw={700} onPress={() => fire(EV.RESUME)}>재개</Btn>
          <Btn variant="ghostDark" w={190} fs={23} onPress={() => fire(EV.END)}>종료</Btn>
        </div>
      </div>
    </Dark>
  )
}
