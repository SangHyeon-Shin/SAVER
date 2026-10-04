import { useEffect, useRef, useState, useCallback } from 'react'

/* ============================================================
   UI ↔ 젯슨 브리지 (WebSocket)

     React UI ──ws://<젯슨>:8765── bridge.py ──USB Serial── Teensy 4.1

   ★ 상태머신의 주인은 브리지입니다. UI 는 화면입니다.
     연결 상태  UI 는 intent 를 보내고, 받은 state 를 그린다
     미연결     UI 가 로컬 전이로 대체 (오프라인 시연 전용)

   받는 메시지
     {type:'state',        node,state,ui,screen,name,alarm,running,allowed}
     {type:'telemetry',    cp_depth_mm, cp_rate_cpm, cp_force_N, vac_kPa, stale}
     {type:'error',        code, sev, why, desc, latched}
     {type:'device_event', evt}
     {type:'link',         connected, port}
     {type:'rejected',     name, node, allowed, reason, mcu, need}
     {type:'search',       phase, reason, points[{x,F,ref,r}], tip_x, D_mm, target_x, pos_x, rule, ...}
     {type:'config',       aed_pause_warn_s}   (AED 압박 중단 경고 기준, 비어 있으면 경고 안 함)

   보내는 메시지 (intent, force 외)
     {type:'search', action:'start'|'stop'}   압박점 탐색 시작·중지 (X_SEARCH 에서만)

   VITE_WS_URL=off 로 빌드하면 연결하지 않는다(브라우저만으로 보는 체험판).
   ============================================================ */

const WS_URL =
  import.meta.env.VITE_WS_URL ||
  `ws://${typeof location !== 'undefined' ? location.hostname : 'localhost'}:8765`
const DISABLED = WS_URL === 'off'

export function useBridge({ onState, onRejected } = {}) {
  const [connected, setConnected] = useState(false)
  const [telemetry, setTelemetry] = useState(null)
  const [stale, setStale] = useState(false)
  const [linkUp, setLinkUp] = useState(false)     // 젯슨↔Teensy 링크
  const [lastError, setLastError] = useState(null)
  const [rejected, setRejected] = useState(null)
  const [search, setSearch] = useState(null)       // 브리지가 보낸 마지막 탐색 진행
  const [config, setConfig] = useState(null)       // 브리지 설정(aed_pause_warn_s 등)

  const wsRef = useRef(null)
  const cbState = useRef(onState); cbState.current = onState
  const cbReject = useRef(onRejected); cbReject.current = onRejected

  useEffect(() => {
    if (DISABLED) return undefined
    let closed = false
    let retry

    const connect = () => {
      if (closed) return
      let ws
      try { ws = new WebSocket(WS_URL) } catch { retry = setTimeout(connect, 3000); return }
      wsRef.current = ws

      ws.onopen = () => setConnected(true)
      ws.onclose = () => {
        // 끊기면 마지막 텔레메트리를 실측처럼 보이지 않게 stale 로 둔다(새 tel 이 오면 풀림)
        setConnected(false); setLinkUp(false); setStale(true)
        if (!closed) retry = setTimeout(connect, 3000)      // 자동 재연결
      }
      ws.onerror = () => ws.close()
      ws.onmessage = (e) => {
        let m
        try { m = JSON.parse(e.data) } catch { return }
        switch (m.type) {
          case 'state':
            cbState.current && cbState.current(m); break
          case 'telemetry':
            setStale(!!m.stale)
            if (!m.stale) setTelemetry(m)
            break
          case 'error':
            setLastError(m); break
          case 'link':
            setLinkUp(!!m.connected)
            if (!m.connected) setStale(true)        // 링크가 끊기면 브리지가 stale 을 따로 보내지 않는다
            break
          case 'search':
            setSearch(m); break
          case 'config':
            setConfig(m); break
          case 'rejected':
            setRejected(m)
            cbReject.current && cbReject.current(m)
            setTimeout(() => setRejected(null), 2500)
            break
          default: break
        }
      }
    }

    connect()
    return () => { closed = true; clearTimeout(retry); wsRef.current && wsRef.current.close() }
  }, [])

  /** 상태 전이 요청. 판단은 브리지가 한다. */
  const sendIntent = useCallback((name, payload) => {
    const ws = wsRef.current
    if (!ws || ws.readyState !== WebSocket.OPEN) return false
    ws.send(JSON.stringify({ type: 'intent', name, payload, ts: Date.now() }))
    return true
  }, [])

  /** 시연 패널 전용 강제 이동 */
  const sendForce = useCallback((node) => {
    const ws = wsRef.current
    if (!ws || ws.readyState !== WebSocket.OPEN) return false
    ws.send(JSON.stringify({ type: 'force', node }))
    return true
  }, [])

  /** 압박점 탐색 시작·중지 요청. 동작 판단은 브리지와 Teensy 가 한다. */
  const sendSearch = useCallback((action) => {
    const ws = wsRef.current
    if (!ws || ws.readyState !== WebSocket.OPEN) return false
    ws.send(JSON.stringify({ type: 'search', action }))
    return true
  }, [])

  const clearSearch = useCallback(() => setSearch(null), [])

  return { connected, linkUp, telemetry, stale, lastError, rejected, search, config,
           sendIntent, sendForce, sendSearch, clearSearch, url: DISABLED ? '(연결 안 함)' : WS_URL }
}
