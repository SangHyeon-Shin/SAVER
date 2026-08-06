// POSIX의 `SINGLE=1 vite build` 문법은 Windows cmd.exe에서 실행되지 않는다.
// Node에서 환경변수를 설정한 뒤 Vite API를 호출해 두 환경에서 같은 빌드를 만든다.
process.env.SINGLE = '1'

const { build } = await import('vite')
await build()
