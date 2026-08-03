// SAVER 디자인 토큰 (KRDS 기반) — 디자인 원본 SAVER_Claude_Design_프롬프트.md [0] 공통 디자인 시스템
export const C = {
  primary: '#246BEB',   // Primary (파랑) — 기본 행동 버튼
  danger:  '#D82828',   // Danger  (빨강) — 위험 / 정지
  warning: '#F26000',   // Warning (주황) — 경고
  caution: '#E8A800',   // Caution (노랑) — 주의
  success: '#1E8E3E',   // Success (초록) — 정상 / 진행

  darkBg:   '#14181E',  // 다크 테마 배경
  darkPane: '#0f1319',  // 다크 그래프 패널
  darkCard: '#1b212b',  // 다크 수치 카드
  darkLine: '#232b37',
  darkText: '#E6E6E6',
  darkSub:  '#8b94a3',

  lightBg:  '#ffffff',
  lightLine:'#DCDCDC',
  lightSub: '#6b7280',

  wave:   '#00C8FF',    // 압박 리듬 파형
  ppg:    '#3ee08f',    // PPG 파형
  breath: '#9b8cff',    // 호흡 파형
}

// 알람 4등급 (연동표 v2 §1)
export const ALARM = {
  normal:  { key: 'normal',  label: '정상', color: C.success, band: 0  },
  caution: { key: 'caution', label: '주의', color: C.caution, band: 12 },
  warning: { key: 'warning', label: '경고', color: C.warning, band: 14 },
  danger:  { key: 'danger',  label: '위험', color: C.danger,  band: 0  },
}

// 장치 화면 물리 해상도 (7인치 IPS 터치)
export const SCREEN_W = 1024
export const SCREEN_H = 600
