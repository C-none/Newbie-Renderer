import { defineConfig, loadEnv } from 'vite';
import vue from '@vitejs/plugin-vue';
import stylelint from 'vite-plugin-stylelint';
import svgLoader from 'vite-svg-loader';
import autoImport from 'unplugin-auto-import/vite';
import { resolve } from 'path';

export default ({ mode }) => {
  const env = loadEnv(mode, import.meta.dirname);

  return defineConfig({
    base: env.VITE_BASE_PUBLIC_PATH,
    root: import.meta.dirname,
    publicDir: 'assets/exr',
    plugins: [
      stylelint(),
      svgLoader(),
      vue(),
      autoImport({
        imports: [
          'vue',
          'vue-router',
        ],
        eslintrc: {
          enabled: false,
        },
        dirs: [
          resolve(import.meta.dirname, 'src/components'),
          resolve(import.meta.dirname, 'src/composables'),
        ],
      }),
    ],
    resolve: {
      alias: {
        '@': resolve(import.meta.dirname, 'src'),
      },
    },
  });
};
