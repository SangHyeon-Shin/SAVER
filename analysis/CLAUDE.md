# analysis/ 작업 규칙

- 정역학·동역학 계산 스크립트/노트북을 둔다. 결과 수치가 `docs/PROJECT_STATE.md`의
  확정값(최대 압박력 555.1 N 등)과 다르면, 재계산이 맞는지 확정값 갱신이 맞는지
  먼저 판단하고 `docs/DECISIONS.md`에 기록한 뒤에만 확정값을 바꾼다.
- CFD(`cfd/`) 결과를 인용할 때는 `simulated`로 표기하고 경계조건(dead-head 가정 등)을
  함께 적는다.
