import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue';
import stylelint from 'vite-plugin-stylelint';
import svgLoader from 'vite-svg-loader';
import { resolve } from 'path';

export default defineConfig({
  base: '/Newbie-Renderer/',
  root: import.meta.dirname,
  publicDir: 'assets/exr',
  plugins: [
    stylelint(),
    svgLoader(),
    vue(),
  ],
  resolve: {
    alias: {
      '@': resolve(import.meta.dirname, 'src'),
    },
  },
});
