/* 음성 재생기. 파일은 voice/<ID>.mp3 (ui/public/voice → dist/voice).
   - 화면이 바뀌면 하던 말을 끊고 새 화면 문구를 말한다
   - 같은 화면의 사건 음성은 끊지 않고 뒤에 잇는다
   - 파일이 없거나 재생이 막히면(자동 재생 정책) 조용히 건너뛴다. 음성은 보조이고 제어와 무관하다
   - 음소거, 다시 듣기(마지막으로 시작한 묶음)
   jsdom(smoke) 에서는 소리를 내지 않고 순서만 기록한다(makeAudio 가 null)          */
import { planEnter, planEvent } from './plan.js'

const defaultAudio = (src) =>
  (typeof Audio === 'undefined' || /jsdom/.test(navigator.userAgent)) ? null : new Audio(src)

export function createVoice({ prompts, base = 'voice/', makeAudio = defaultAudio, log = () => {} }) {
  let muted = false, node = null, queue = [], cur = null, last = []
  const played = []                         // 시험용: 재생을 시도한 ID 순서

  const stop = () => {
    queue = []
    if (cur) { const a = cur; cur = null; a.onended = a.onerror = null; try { a.pause() } catch (e) { /* 무시 */ } }
  }
  const next = () => {
    if (muted) { queue = []; return }
    if (cur || !queue.length) return
    const id = queue.shift()
    played.push(id); if (played.length > 200) played.shift()
    let a = null
    try { a = makeAudio(base + id + '.mp3') } catch (e) { a = null }
    if (!a) { next(); return }
    cur = a
    const done = () => { if (cur !== a) return; cur = null; next() }
    a.onended = done
    a.onerror = () => { log('voice_missing', id); done() }
    let r
    try { r = a.play() } catch (e) { log('voice_error', id); done(); return }
    if (r && r.catch) r.catch((e) => { log('voice_blocked', id, e && e.name); done() })
  }

  return {
    /** 화면 진입: 하던 말을 끊고 새 화면 문구 */
    enter(n, opt) { node = n; stop(); last = planEnter(prompts, n, opt); queue = last.slice(); next() },
    /** 같은 화면의 사건: 끊지 않고 뒤에 잇는다 */
    event(when) {
      const ids = planEvent(prompts, node, when)
      if (!ids.length) return
      last = ids; queue = queue.concat(ids); next()
    },
    /** 다시 듣기: 마지막으로 시작한 묶음 */
    replay() { if (last.length) { stop(); queue = last.slice(); next() } },
    setMuted(m) { muted = !!m; if (muted) stop() },
    get muted() { return muted },
    get lastIds() { return last.slice() },
    played,
    stop,
  }
}
