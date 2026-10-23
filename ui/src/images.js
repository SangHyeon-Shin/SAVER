/* 화면에서 쓰는 그림.
   patient_angled, patient_topdown, clothes 는 전달패키지의 원본 PNG 입니다.
   예전에 받은 같은 이름의 PNG 는 내보내기가 중단되어 위쪽 23~28% 만 내용이 있어
   SVG 일러스트로 대신했었고, 온전한 원본은 PR #1(agent/mr60-vitals-ui)에 있던 것을 가져왔습니다.
   SVG(patient_*.svg, clothes_*.svg)는 대체용으로 남겨 두며 지금은 쓰지 않습니다.

   clothes.png 는 한 장에 왼쪽 외투를 입은 상체, 가운데 화살표, 오른쪽 외투를 벗긴 상체가 있습니다.
   patient_topdown.png 는 화면 06/15 의 초록 유도선(가로 31%, 세로 50%)이 원래 디자인대로
   objectFit 'cover' 에서 겹쳐지도록 그려져 있습니다. */
import device from './assets/device.png'
import ppgSensor from './assets/ppg_sensor.png'
import clothes from './assets/clothes.png'
import patientAngled from './assets/patient_angled.png'
import patientTopdown from './assets/patient_topdown.png'

export const IMG = {
  device,
  ppg_sensor: ppgSensor,
  clothes,
  patient_angled: patientAngled,
  patient_topdown: patientTopdown,
}
