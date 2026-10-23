/* ============================================================
   SAVER 운용 상태머신 (S1~S13) — UI 측
   ★ 이 파일은 전이 규칙을 정의하지 않습니다.
     규칙은 protocol/state_machine_v2.json 이 유일한 기준이며,
     Jetson 의 jetson/saver/state_machine.py 도 같은 파일을 읽습니다.
     상태나 이벤트를 바꾸려면 그 JSON 만 고치세요.

   generated/state_machine_v2.json 은 npm run dev / build 직전에
   scripts/sync-protocol.mjs 가 자동 복사합니다. 직접 편집 금지.
   ============================================================ */
import spec from './generated/state_machine_v2.json'
import { redirectTarget } from './redirect'

/** 이벤트 상수. EV.CPR_START 처럼 씁니다 (오타를 조기에 잡기 위함) */
export const EV = Object.freeze(
  Object.fromEntries(Object.keys(spec.events).map((k) => [k, k]))
)

/** 연동표 v2 수록 여부: 'std' | 'ext' */
export const EV_KIND = Object.freeze({ ...spec.events })

/** 알람 등급 */
export const LV = Object.freeze({
  NORMAL: 'normal', CAUTION: 'caution', WARNING: 'warning', DANGER: 'danger',
})

/** 노드 정의 (JSON 에서 파생) */
export const NODES = Object.freeze(
  Object.fromEntries(
    Object.entries(spec.nodes).map(([id, n]) => [
      id,
      {
        id,
        state: n.state || null,
        ui: n.ui || '-',
        screen: n.screen || 0,
        name: n.name || id,
        alarm: n.alarm || LV.NORMAL,
        running: !!n.running,
        // 자동 전이는 오프라인 시연 전용(offline_auto, v2.4). 실기에서는 브리지가 상태를 정한다
        auto: n.offline_auto || n.auto || null,
        on: n.on || {},
        errorReturn: n.error_return || null,
        enterCmds: n.enter_cmds || [],
        requiresMcu: n.requires_mcu || null,
        eventRequiresMcu: n.event_requires_mcu || {},
        redirect: n.redirect || {},
      },
    ])
  )
)

export const INITIAL = {
  node: spec.initial,
  errorReturn: 'S2',
  errorCause: null,
  errorWhy: null,
  battery: false,
  patient: { sex: 'male', age: 'adult' },
  log: [],
}

/**
 * 상태 전이 (오프라인 데모 전용).
 *
 * 브리지에 연결되어 있을 때는 이 함수를 쓰지 않습니다.
 * UI 는 intent 를 보내고 브리지가 돌려준 state 를 그리기만 합니다.
 * 여기 로직은 젯슨 없이 UI 만 시연할 때의 대체 경로입니다.
 */
export function transition(ctx, event, payload = {}) {
  const cur = NODES[ctx.node]
  const target = cur.on && cur.on[event]
  if (!target) return { next: ctx, changed: false }

  // 이벤트별 장치 조건(v2.3, Jetson state_machine.py 와 같은 규칙). 장치 상태를 모르면(오프라인) 검사하지 않는다.
  const need = cur.eventRequiresMcu[event]
  if (need && payload.mcu && !need.includes(payload.mcu)) return { next: ctx, changed: false }

  let toId = target === '@return' ? (ctx.errorReturn || 'S2') : target
  // 장치 플래그에 따른 목적지 변경 (Jetson state_machine.py 와 같은 규칙, redirect.js).
  // 오프라인 데모에는 장치 플래그가 없으므로 payload.mcuFlags 가 있을 때만 검사한다.
  const redirected = redirectTarget(cur.redirect[event], payload.mcuFlags)
  if (redirected) toId = redirected
  const next = { ...ctx, node: toId }

  if (toId === 'S10') {
    // 이벤트별 복귀 노드(v2.4, Jetson 과 같은 규칙): 초기화 오류는 어디서 났든 S1 에서 다시 점검
    next.errorReturn = (spec.error_return_by_event || {})[event] || cur.errorReturn || 'S2'
    next.errorCause =
      event === EV.POSITION_ERROR ? 'position' :
      event === EV.VACUUM_ERROR ? 'vacuum' :
      event === EV.INIT_ERROR ? 'init' :
      payload.cause || 'tubing'
    next.errorWhy = payload.why || null
  } else {
    next.errorCause = null
    next.errorWhy = null
  }

  if (payload.patient) next.patient = { ...ctx.patient, ...payload.patient }

  next.log = [
    ...ctx.log.slice(-199),
    { ts: Date.now(), from: cur.id, event, to: toId, state: NODES[toId].state },
  ]
  return { next, changed: true }
}

/** 배터리 오버레이는 상태 전이와 독립 */
export function setBattery(ctx, on) {
  return { ...ctx, battery: on }
}
