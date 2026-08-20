<script setup>
import * as THREE from 'three';
import { EXRLoader } from 'three/addons/loaders/EXRLoader.js';
import {
  computed,
  nextTick,
  onBeforeUnmount,
  onMounted,
  ref,
  watch,
} from 'vue';

const galleryItems = [
  {
    name: 'chess.exr',
    size: '6.6 MB',
    title: 'Chess',
    description: 'The chess set demonstrates transmission and volume across its glass, stone, and metal materials.',
    credit: 'A Beautiful Game — MaterialX Project and Ed Mackey, CC BY 4.0',
    sourceUrl: 'https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/ABeautifulGame',
  },
  {
    name: 'anisotropic lamp.exr',
    size: '8.7 MB',
    title: 'Anisotropic Lamp',
    description: 'The brushed-metal lamp demonstrates support for anisotropic materials and their directional highlights.',
    credit: 'Anisotropy Barn Lamp — Wayfair and Eric Chadwick, CC BY 4.0',
    sourceUrl: 'https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/AnisotropyBarnLamp',
  },
  {
    name: 'helmet.exr',
    size: '16.4 MB',
    title: 'Damaged Helmet',
    description: 'The weathered helmet demonstrates metallic-roughness PBR, normal mapping, and image-based environment lighting.',
    credit: 'Damaged Helmet — theblueturtle_ and ctxwing, CC BY-NC 4.0 and CC BY 4.0',
    sourceUrl: 'https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet',
  },
  {
    name: 'sponza.exr',
    size: '16.4 MB',
    title: 'Sponza',
    description: 'The Sponza scene demonstrates path-traced textured architecture with alpha-masked geometry.',
    credit: 'Sponza — Crytek, CryEngine Limited License Agreement',
    sourceUrl: 'https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza',
  },
];

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
const selectedGalleryIndex = ref(0);
const exposure = ref(1);
const selectedToneMapping = ref('aces-filmic');
const loadState = ref('idle');
const loadProgress = ref(0);
const errorMessage = ref('');
const imageSize = ref('');
const selectedGalleryItem = computed(() => galleryItems[selectedGalleryIndex.value]);

function selectGalleryItem(index) {
  if (loadState.value === 'loading' || index === selectedGalleryIndex.value) return;
  selectedGalleryIndex.value = index;
}

function showPreviousGalleryItem() {
  if (loadState.value === 'loading') return;
  selectedGalleryIndex.value = (selectedGalleryIndex.value - 1 + galleryItems.length) % galleryItems.length;
}

function showNextGalleryItem() {
  if (loadState.value === 'loading') return;
  selectedGalleryIndex.value = (selectedGalleryIndex.value + 1) % galleryItems.length;
}

function handleViewerKeydown(event) {
  if (event.target !== event.currentTarget) return;

  if (event.key === 'ArrowLeft') {
    event.preventDefault();
    showPreviousGalleryItem();
  } else if (event.key === 'ArrowRight') {
    event.preventDefault();
    showNextGalleryItem();
  }
}

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
    assetUrl(selectedGalleryItem.value.name),
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
watch(selectedGalleryIndex, loadSelectedExr, { flush: 'sync' });

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
  <div class="grid gap-4 rounded-2xl border border-slate-800 bg-slate-900/70 p-4 shadow-2xl shadow-black/30 lg:grid-cols-[18rem_minmax(0,1fr)]">
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
          {{ selectedGalleryItem.name }}
        </dd>
        <dt class="text-slate-500">
          File size
        </dt>
        <dd class="text-right text-slate-300">
          {{ selectedGalleryItem.size }}
        </dd>
        <dt class="text-slate-500">
          Resolution
        </dt>
        <dd class="text-right text-slate-300">
          {{ statusText() || '—' }}
        </dd>
      </dl>
    </aside>

    <figure
      class="relative min-h-[30rem] overflow-hidden rounded-xl border border-slate-800 bg-[#090b10] outline-none focus-visible:ring-2 focus-visible:ring-cyan-400 lg:min-h-[55vh]"
      tabindex="0"
      :aria-busy="loadState === 'loading'"
      :aria-label="`EXR gallery viewer: ${selectedGalleryItem.title}`"
      aria-keyshortcuts="ArrowLeft ArrowRight"
      @keydown="handleViewerKeydown"
    >
      <div
        ref="canvasHost"
        class="absolute inset-0"
      />

      <div
        v-if="loadState === 'loading'"
        class="pointer-events-none absolute inset-0 z-10 grid place-items-center bg-slate-950/70 backdrop-blur-sm"
        role="status"
      >
        <div class="flex flex-col items-center gap-3 text-sm text-slate-300">
          <span class="size-8 animate-spin rounded-full border-2 border-slate-700 border-t-cyan-400" />
          <span>{{ statusText() }}</span>
        </div>
      </div>

      <div
        v-if="loadState === 'error'"
        class="absolute inset-0 z-10 grid place-items-center p-6 pb-40 text-center sm:pb-32"
        role="alert"
      >
        <div class="max-w-lg rounded-xl border border-red-500/30 bg-red-950/60 p-5">
          <p class="font-semibold text-red-200">
            EXR could not be displayed
          </p>
          <p class="mt-2 break-words text-sm text-red-300/80">
            {{ errorMessage }}
          </p>
          <button
            class="mt-4 rounded-lg bg-red-400 px-4 py-2 text-sm font-semibold text-red-950 transition hover:bg-red-300 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-red-200"
            type="button"
            @click="loadSelectedExr"
          >
            Try again
          </button>
        </div>
      </div>

      <figcaption class="absolute inset-x-0 bottom-0 z-20 flex flex-col gap-4 border-t border-white/10 bg-slate-950/85 p-4 backdrop-blur-md sm:flex-row sm:items-end sm:justify-between">
        <div
          class="max-w-2xl"
          aria-atomic="true"
          aria-live="polite"
        >
          <p class="font-mono text-xs text-cyan-300">
            {{ String(selectedGalleryIndex + 1).padStart(2, '0') }} / {{ String(galleryItems.length).padStart(2, '0') }}
          </p>
          <h3 class="mt-1 text-lg font-semibold text-white">
            {{ selectedGalleryItem.title }}
          </h3>
          <p class="mt-1 text-sm leading-6 text-slate-300">
            {{ selectedGalleryItem.description }}
          </p>
          <a
            class="mt-2 inline-block text-xs text-slate-400 underline decoration-slate-600 underline-offset-4 transition hover:text-cyan-300"
            :href="selectedGalleryItem.sourceUrl"
            rel="noreferrer"
            target="_blank"
          >
            Asset credit: {{ selectedGalleryItem.credit }}
          </a>
        </div>

        <nav
          class="flex max-w-full flex-wrap items-center justify-end gap-1 self-end sm:gap-2"
          aria-label="Gallery pagination"
        >
          <button
            class="grid size-8 place-items-center rounded-full border border-slate-600 bg-slate-900/90 text-lg text-slate-100 transition hover:border-cyan-300 hover:text-cyan-200 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-cyan-300 disabled:cursor-wait disabled:opacity-50 sm:size-9"
            type="button"
            aria-label="Show previous gallery image"
            :disabled="loadState === 'loading'"
            @click="showPreviousGalleryItem"
          >
            <span aria-hidden="true">←</span>
          </button>
          <button
            v-for="(item, index) in galleryItems"
            :key="item.name"
            class="grid size-8 place-items-center rounded-full border text-xs font-semibold transition focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-cyan-300 disabled:cursor-wait disabled:opacity-50 sm:size-9"
            :class="index === selectedGalleryIndex
              ? 'border-cyan-300 bg-cyan-300 text-slate-950'
              : 'border-slate-600 bg-slate-900/90 text-slate-300 hover:border-cyan-300 hover:text-cyan-200'"
            type="button"
            :aria-label="`Show ${item.title}`"
            :aria-current="index === selectedGalleryIndex ? 'page' : undefined"
            :disabled="loadState === 'loading'"
            @click="selectGalleryItem(index)"
          >
            {{ index + 1 }}
          </button>
          <button
            class="grid size-8 place-items-center rounded-full border border-slate-600 bg-slate-900/90 text-lg text-slate-100 transition hover:border-cyan-300 hover:text-cyan-200 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-cyan-300 disabled:cursor-wait disabled:opacity-50 sm:size-9"
            type="button"
            aria-label="Show next gallery image"
            :disabled="loadState === 'loading'"
            @click="showNextGalleryItem"
          >
            <span aria-hidden="true">→</span>
          </button>
        </nav>
      </figcaption>
    </figure>
  </div>
</template>
