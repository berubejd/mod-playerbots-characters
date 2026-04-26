import { defineConfig, loadEnv } from 'vite';
import preact from '@preact/preset-vite';

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '');

  return {
    plugins: [preact()],
    build: {
      outDir: 'dist',
      sourcemap: false,
    },
    server: {
      proxy: {
        '/api': env.VITE_DEV_API_URL || 'http://127.0.0.1:8501',
        '/ws': {
          target: env.VITE_DEV_WS_URL || 'ws://127.0.0.1:8501',
          ws: true,
        },
      },
    },
  };
});
