export const MR60_USB_VENDOR_ID = 0x303A
export const MR60_BAUD_RATE = 115200

function numberOrNull(value) {
  if (!value || value.toUpperCase() === 'NO_DATA') return null
  const parsed = Number(value)
  return Number.isFinite(parsed) ? parsed : null
}

/**
 * XIAO/MR60BHA2 펌웨어의 CSV 한 줄을 UI 측정값으로 변환한다.
 * 부팅 로그, 헤더, 깨진 줄은 null로 무시하며 NO_DATA는 실제 0과 구분한다.
 */
export function parseMr60Line(line, receivedAt = Date.now()) {
  const text = String(line || '').trim()
  if (!text || text.startsWith('heart_rate_bpm')) return null

  const fields = text.split(',').map((value) => value.trim())
  if (fields.length !== 6) return null

  const sample = {
    at: receivedAt,
    heartRateBpm: numberOrNull(fields[0]),
    breathRateBrpm: numberOrNull(fields[1]),
    breathPeriodSec: numberOrNull(fields[2]),
    heartStatus: fields[3] || 'NO_DATA',
    breathStatus: fields[4] || 'NO_DATA',
    distanceCm: numberOrNull(fields[5]),
  }

  const hasMeasurement = sample.heartRateBpm != null || sample.breathRateBrpm != null ||
    sample.breathPeriodSec != null || sample.distanceCm != null
  return hasMeasurement ? sample : null
}
