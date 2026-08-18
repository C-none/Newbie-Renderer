<script setup>
import * as THREE from 'three';
import { EXRLoader } from 'three/addons/loaders/EXRLoader.js';
import {
  nextTick,
  onBeforeUnmount,
  onMounted,
  ref,
  watch,
} from 'vue';

const exrFile = { name: 'chess.exr', size: '6.6 MB' };
const toneMappingOptions = [
  { label: 'None', value: 'none', mode: THREE.NoToneMapping },
  { label: 'Linear', value: 'linear', mode: THREE.LinearToneMapping },
  { label: 'Reinhard', value: 'reinhard', mode: THREE.ReinhardToneMapping },
  { label: 'Cineon', value: 'cineon', mode: THREE.CineonToneMapping },
  { label: 'ACES Filmic', value: 'aces-filmic', mode: THREE.ACESFilmicToneMapping },
  { label: 'AgX', value: 'agx', mode: THREE.AgXToneMapping },
  { label: 'Neutral', value: 'neutral', mode: THREE.NeutralToneMapping },
];

const canvasHost = ref(null);
const exposure = ref(1);
const selectedToneMapping = ref('aces-filmic');
const loadState = ref('idle');
const loadProgress = ref(0);
const errorMessage = ref('');
const imageSize = ref('');

function statusText() {
  if (loadState.value === 'loading') {
    return loadProgress.value > 0 ? `Loading ${loadProgress.value}%` : 'Loading EXR…';
  }
  if (loadState.value === 'ready') {
    return imageSize.value;
  }
  return '';
}

let renderer;
let scene;
let camera;
let previewMesh;
let previewTexture;
let resizeObserver;
let loadSequence = 0;

function render() {
  if (!renderer || !scene || !camera) return;
  renderer.toneMappingExposure = exposure.value;
  renderer.render(scene, camera);
}

function updateToneMapping() {
  if (!renderer) return;

  const option = toneMappingOptions.find(({ value }) => value === selectedToneMapping.value);
  renderer.toneMapping = option?.mode ?? THREE.ACESFilmicToneMapping;

  if (previewMesh) {
    previewMesh.material.needsUpdate = true;
  }

  render();
}

function fitPreview() {
  if (!renderer || !camera || !previewMesh || !canvasHost.value) return;

  const width = Math.max(canvasHost.value.clientWidth, 1);
  const height = Math.max(canvasHost.value.clientHeight, 1);
  const viewportAspect = width / height;

  camera.left = -viewportAspect;
  camera.right = viewportAspect;
  camera.top = 1;
  camera.bottom = -1;
  camera.updateProjectionMatrix();
  renderer.setSize(width, height, false);

  if (previewTexture) {
    const imageAspect = previewTexture.image.width / previewTexture.image.height;
    if (imageAspect > viewportAspect) {
      previewMesh.scale.set(2 * viewportAspect, (2 * viewportAspect) / imageAspect, 1);
    } else {
      previewMesh.scale.set(2 * imageAspect, 2, 1);
    }
  }

  render();
}

function assetUrl(filename) {
  return `${import.meta.env.BASE_URL}${encodeURIComponent(filename)}`;
}

function loadSelectedExr() {
  if (!renderer) return;

  loadSequence += 1;
  const sequence = loadSequence;
  loadState.value = 'loading';
  loadProgress.value = 0;
  errorMessage.value = '';
  imageSize.value = '';

  new EXRLoader().load(
    assetUrl(exrFile.name),
    (texture) => {
      if (sequence !== loadSequence) {
        texture.dispose();
        return;
      }

      previewTexture?.dispose();
      previewTexture = texture;
      previewMesh.material.map = texture;
      previewMesh.material.needsUpdate = true;
      imageSize.value = `${texture.image.width} × ${texture.image.height}`;
      loadState.value = 'ready';
      fitPreview();
    },
    ({ loaded, total }) => {
      if (sequence === loadSequence && total > 0) {
        loadProgress.value = Math.round((loaded / total) * 100);
      }
    },
    (error) => {
      if (sequence !== loadSequence) return;
      loadState.value = 'error';
      errorMessage.value = error?.message || 'Unable to decode this EXR file.';
    },
  );
}

onMounted(async () => {
  await nextTick();

  try {
    renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    updateToneMapping();

    scene = new THREE.Scene();
    scene.background = new THREE.Color(0x090b10);
    camera = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
    previewMesh = new THREE.Mesh(
      new THREE.PlaneGeometry(1, 1),
      new THREE.MeshBasicMaterial({ toneMapped: true }),
    );
    scene.add(previewMesh);

    renderer.domElement.className = 'block size-full';
    canvasHost.value.appendChild(renderer.domElement);
    resizeObserver = new ResizeObserver(fitPreview);
    resizeObserver.observe(canvasHost.value);
    fitPreview();
    loadSelectedExr();
  } catch (error) {
    loadState.value = 'error';
    errorMessage.value = error?.message || 'WebGL is unavailable in this browser.';
  }
});

watch(exposure, render);
watch(selectedToneMapping, updateToneMapping);

onBeforeUnmount(() => {
  loadSequence += 1;
  resizeObserver?.disconnect();
  previewTexture?.dispose();
  previewMesh?.geometry.dispose();
  previewMesh?.material.dispose();
  renderer?.dispose();
  renderer?.domElement.remove();
});
</script>

<template>
  <main class="min-h-screen bg-slate-950 px-4 py-8 text-slate-100 sm:px-8">
    <div class="mx-auto flex w-full max-w-7xl flex-col gap-6">
      <header class="flex flex-col gap-2">
        <p class="text-sm font-semibold uppercase tracking-[0.24em] text-cyan-400">
          Newbie Renderer
        </p>
        <h1 class="text-3xl font-semibold tracking-tight sm:text-5xl">
          EXR environment gallery
        </h1>
        <p class="max-w-3xl text-sm leading-6 text-slate-400 sm:text-base">
          OpenEXR assets are decoded locally and displayed with selectable tone mapping.
          Only the selected image is loaded to keep memory usage predictable.
        </p>
      </header>

      <section class="grid gap-4 rounded-2xl border border-slate-800 bg-slate-900/70 p-4 shadow-2xl shadow-black/30 lg:grid-cols-[18rem_minmax(0,1fr)]">
        <aside class="flex flex-col gap-5 rounded-xl bg-slate-950/60 p-4">
          <label class="flex flex-col gap-2 text-sm font-medium text-slate-300">
            <span>Tone mapping</span>
            <select
              v-model="selectedToneMapping"
              class="rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-slate-100 outline-none transition focus:border-cyan-400 focus:ring-2 focus:ring-cyan-400/20"
            >
              <option
                v-for="option in toneMappingOptions"
                :key="option.value"
                :value="option.value"
              >
                {{ option.label }}
              </option>
            </select>
          </label>

          <label class="flex flex-col gap-2 text-sm font-medium text-slate-300">
            <span class="flex items-center justify-between">
              <span>Exposure</span>
              <output class="font-mono text-cyan-300">{{ exposure.toFixed(2) }}</output>
            </span>
            <input
              v-model.number="exposure"
              class="accent-cyan-400"
              type="range"
              min="0.1"
              max="4"
              step="0.05"
            >
          </label>

          <dl class="grid grid-cols-[auto_1fr] gap-x-3 gap-y-2 border-t border-slate-800 pt-4 text-xs">
            <dt class="text-slate-500">
              File
            </dt>
            <dd class="break-all text-right text-slate-300">
              {{ exrFile.name }}
            </dd>
            <dt class="text-slate-500">
              Download
            </dt>
            <dd class="text-right text-slate-300">
              {{ exrFile.size }}
            </dd>
            <dt class="text-slate-500">
              Resolution
            </dt>
            <dd class="text-right text-slate-300">
              {{ statusText() || '—' }}
            </dd>
          </dl>
        </aside>

        <div class="relative min-h-[55vh] overflow-hidden rounded-xl border border-slate-800 bg-[#090b10]">
          <div
            ref="canvasHost"
            class="absolute inset-0"
          />

          <div
            v-if="loadState === 'loading'"
            class="absolute inset-0 grid place-items-center bg-slate-950/70 backdrop-blur-sm"
          >
            <div class="flex flex-col items-center gap-3 text-sm text-slate-300">
              <span class="size-8 animate-spin rounded-full border-2 border-slate-700 border-t-cyan-400" />
              <span>{{ statusText() }}</span>
            </div>
          </div>

          <div
            v-if="loadState === 'error'"
            class="absolute inset-0 grid place-items-center p-6 text-center"
          >
            <div class="max-w-lg rounded-xl border border-red-500/30 bg-red-950/60 p-5">
              <p class="font-semibold text-red-200">
                EXR could not be displayed
              </p>
              <p class="mt-2 break-words text-sm text-red-300/80">
                {{ errorMessage }}
              </p>
              <button
                class="mt-4 rounded-lg bg-red-400 px-4 py-2 text-sm font-semibold text-red-950 transition hover:bg-red-300"
                type="button"
                @click="loadSelectedExr"
              >
                Try again
              </button>
            </div>
          </div>
        </div>
      </section>
    </div>
  </main>
</template>
