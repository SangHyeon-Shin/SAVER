import { createContext } from 'react'

/** { voice, muted, toggleMute } — App 이 넣는다. 없으면(단독 렌더) 음성 버튼을 그리지 않는다 */
export const VoiceCtx = createContext(null)
