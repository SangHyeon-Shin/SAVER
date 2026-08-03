/* 화면에서 쓰는 그림.
   patient_*, clothes_* 는 SVG 일러스트입니다.
   전달받은 PNG 3장(patient_angled, patient_topdown, clothes)은 내보내기가
   중단되어 위쪽 23~28% 만 내용이 있고 나머지가 순수 검정이었습니다.
   한성윤이 온전한 이미지를 다시 만들면 같은 이름의 png 로 교체하고
   아래 import 경로만 바꾸면 됩니다.

   device.png, ppg_sensor.png 는 원본이 정상이라 그대로 씁니다. */
import device from './assets/device.png'
import ppgSensor from './assets/ppg_sensor.png'
import clothesOn from './assets/clothes_on.svg'
import clothesOff from './assets/clothes_off.svg'
import patientAngled from './assets/patient_angled.svg'
import patientTopdown from './assets/patient_topdown.svg'

export const IMG = {
  device,
  ppg_sensor: ppgSensor,
  clothes_on: clothesOn,
  clothes_off: clothesOff,
  clothes: clothesOn,          // 하위 호환
  patient_angled: patientAngled,
  patient_topdown: patientTopdown,
}
