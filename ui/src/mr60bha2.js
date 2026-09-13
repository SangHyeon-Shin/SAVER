import { useCallback, useEffect, useRef, useState } from 'react'
import { MR60_BAUD_RATE, MR60_USB_VENDOR_ID, parseMr60Line } from './mr60bha2Parser'

const HISTORY_MS = 120_000
const STALE_MS = 3_500

/** 브라우저 Web Serial로 XIAO/MR60BHA2를 읽는 관찰 전용 링크. */
export function useMr60Bha2() {
  const [connected, setConnected] = useState(false)
  const [connecting, setConnecting] = useState(false)
  const [stale, setStale] = useState(false)
  const [sample, setSample] = useState(null)
  const [history, setHistory] = useState([])
  const [error, setError] = useState(null)

  const portRef = useRef(null)
  const readerRef = useRef(null)
  const readingRef = useRef(false)
  const staleTimerRef = useRef(null)

  const armStaleTimer = useCallback(() => {
    clearTimeout(staleTimerRef.current)
    staleTimerRef.current = setTimeout(() => setStale(true), STALE_MS)
  }, [])

  const acceptLine = useCallback((line) => {
    const next = parseMr60Line(line)
    if (!next) return
    setSample(next)
    setStale(false)
    setHistory((previous) => {
      const cutoff = next.at - HISTORY_MS
      return [...previous.filter((item) => item.at >= cutoff), next]
    })
    armStaleTimer()
  }, [armStaleTimer])

  const finishConnection = useCallback(async (message) => {
    readingRef.current = false
    clearTimeout(staleTimerRef.current)

    const reader = readerRef.current
    if (reader) {
      try { await reader.cancel() } catch (_) { /* 이미 닫힌 포트 */ }
    }

    const port = portRef.current
    if (port && !readerRef.current) {
      try { await port.close() } catch (_) { /* 읽기 루프가 닫는 중 */ }
      if (portRef.current === port) portRef.current = null
    }

    setConnected(false)
    setConnecting(false)
    setStale(false)
    if (message) setError(message)
  }, [])

  const readPort = useCallback(async (port) => {
    const decoder = new TextDecoder()
    let pending = ''
    try {
      const reader = port.readable.getReader()
      readerRef.current = reader
      while (readingRef.current) {
        const { value, done } = await reader.read()
        if (done) break
        pending += decoder.decode(value, { stream: true })
        const lines = pending.split(/\r?\n/)
        pending = lines.pop() || ''
        lines.forEach(acceptLine)
      }
    } catch (readError) {
      if (readingRef.current) setError(`센서 읽기 실패: ${readError.message}`)
    } finally {
      if (readerRef.current) {
        try { readerRef.current.releaseLock() } catch (_) { /* 이미 해제됨 */ }
        readerRef.current = null
      }
      try { await port.close() } catch (_) { /* 이미 닫힘 */ }
      if (portRef.current === port) portRef.current = null
      readingRef.current = false
      clearTimeout(staleTimerRef.current)
      setConnected(false)
      setConnecting(false)
      setStale(false)
    }
  }, [acceptLine])

  const connect = useCallback(async () => {
    if (!('serial' in navigator)) {
      setError('최신 Chrome/Edge의 localhost 또는 HTTPS 화면에서만 USB 센서를 연결할 수 있습니다.')
      return false
    }
    if (readingRef.current) return true

    setConnecting(true)
    setError(null)
    try {
      const port = await navigator.serial.requestPort({
        filters: [{ usbVendorId: MR60_USB_VENDOR_ID }],
      })
      await port.open({ baudRate: MR60_BAUD_RATE, bufferSize: 4096 })
      portRef.current = port
      readingRef.current = true
      setConnected(true)
      setConnecting(false)
      setStale(true)
      readPort(port)
      return true
    } catch (connectError) {
      setConnecting(false)
      if (connectError.name !== 'NotFoundError') {
        setError(`센서 연결 실패: ${connectError.message}`)
      }
      return false
    }
  }, [readPort])

  const disconnect = useCallback(() => finishConnection(null), [finishConnection])
  const clear = useCallback(() => {
    setSample(null)
    setHistory([])
  }, [])

  useEffect(() => {
    if (!('serial' in navigator)) return undefined
    const onDisconnect = (event) => {
      if (event.target === portRef.current) finishConnection('MR60BHA2 USB 연결이 끊어졌습니다.')
    }
    navigator.serial.addEventListener('disconnect', onDisconnect)
    return () => navigator.serial.removeEventListener('disconnect', onDisconnect)
  }, [finishConnection])

  useEffect(() => () => {
    readingRef.current = false
    clearTimeout(staleTimerRef.current)
    if (readerRef.current) readerRef.current.cancel().catch(() => {})
  }, [])

  return {
    supported: typeof navigator !== 'undefined' && 'serial' in navigator,
    connected,
    connecting,
    stale,
    sample,
    history,
    error,
    connect,
    disconnect,
    clear,
  }
}
