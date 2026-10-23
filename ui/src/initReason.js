/* 초기화 점검(jetson/saver/init_seq.py)이 멈춘 사유(INIT_ERROR 의 why, 브리지 상태의 error_why) → 오류 화면(S10) 문구.
   10/23 팀장: 비상정지 버튼은 연결하지 않았으므로 초기화 오류 화면에 늘 보이던 비상정지 안내를 뺐다(D-54).
   사유가 비상정지일 때만 비상정지 안내가 나온다. 코드는 괄호에 그대로 남겨 현장에서 장치 담당자에게 전한다 */
export function initReason(why) {
  if (!why) return null
  const w = String(why)
  const tail = (p) => w.slice(p.length)
  const dev = '→ 장치(Teensy) 담당자에게 이 사유를 알린 뒤 재확인을 누르세요.'
  if (w === 'ESTOP' || w === 'HOMING_ESTOP') return { t: '비상정지가 눌려 있습니다', s: '→ 비상정지를 푼 뒤 재확인을 누르세요. (' + w + ')' }
  if (w.startsWith('ARM_REJECTED:')) return { t: '장치가 구동 준비(ARM)를 거부했습니다', s: dev + ' (' + (tail('ARM_REJECTED:') || 'ARM_REJECTED') + ')' }
  if (w.startsWith('HOME_REJECTED:')) return { t: '장치가 원점 복귀(HOME)를 거부했습니다', s: dev + ' (' + (tail('HOME_REJECTED:') || 'HOME_REJECTED') + ')' }
  if (/^(PING|STATUS)_REJECTED:/.test(w)) return { t: '장치가 점검 요청을 거부했습니다', s: dev + ' (' + w + ')' }
  if (w.startsWith('ACK_TIMEOUT')) return { t: '장치가 응답하지 않습니다', s: '→ USB 연결과 장치 전원을 확인한 뒤 재확인을 누르세요. (' + w + ')' }
  if (w === 'NO_STATUS') return { t: '장치 상태를 받지 못했습니다', s: '→ USB 연결을 확인한 뒤 재확인을 누르세요. (NO_STATUS)' }
  if (w === 'FAULT' || w.startsWith('FAULT:')) return { t: '장치가 고장 상태입니다', s: dev + ' (' + w + ')' }
  if (w.startsWith('UNEXPECTED_MCU_STATE:')) return { t: '장치가 점검할 수 없는 상태입니다', s: dev + ' (' + tail('UNEXPECTED_MCU_STATE:') + ')' }
  if (w.startsWith('HOMING_') || w === 'HOME_TIMEOUT') return { t: '원점 복귀를 마치지 못했습니다', s: dev + ' (' + w + ')' }
  if (w.startsWith('FW_VERSION')) return null       // 펌웨어 판 불일치는 fw 안내가 맨 위에 따로 나온다
  return { t: '점검이 멈춘 사유', s: '→ ' + w }
}
