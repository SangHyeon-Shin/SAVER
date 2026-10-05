import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// base:'./' → 빌드 결과(dist)를 file:// 로 열거나 젯슨의 아무 경로에서 서빙해도 동작
export default defineConfig({
  plugins: [react()],
  base: './',
  server: {
    host: '0.0.0.0',   // Codespaces / LAN(아이패드)에서 접속 가능하게
    port: 5173,
    strictPort: false,
  },
  preview: { host: '0.0.0.0', port: 4173 },
  build: {
    outDir: process.env.SINGLE ? (process.env.OUTDIR || 'dist-single') : 'dist',
    assetsDir: 'assets',
    sourcemap: false,
    // SINGLE=1 이면 이미지까지 base64로 인라인 → 단일 HTML 생성 가능
    assetsInlineLimit: process.env.SINGLE ? 100_000_000 : 4096,
    // 단일 파일은 file:// 에서도 열려야 하므로 ES module 대신 iife 번들로 생성
    rollupOptions: process.env.SINGLE
      ? { output: { format: 'iife', inlineDynamicImports: true, entryFileNames: 'assets/[name].js' } }
      : {},
  },
})
