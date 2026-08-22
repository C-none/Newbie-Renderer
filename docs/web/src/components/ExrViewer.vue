<script setup>
import {
  computed,
  nextTick,
  onBeforeUnmount,
  ref,
  watch,
} from 'vue';

import { galleryItems, galleryMetadata } from '@/data/gallery.js';

const toneMappingOptions = [
  { label: 'None', value: 'none', modeName: 'NoToneMapping' },
  { label: 'Linear', value: 'linear', modeName: 'LinearToneMapping' },
  { label: 'Reinhard', value: 'reinhard', modeName: 'ReinhardToneMapping' },
  { label: 'Cineon', value: 'cineon', modeName: 'CineonToneMapping' },
  { label: 'ACES Filmic', value: 'aces-filmic', modeName: 'ACESFilmicToneMapping' },
  { label: 'AgX', value: 'agx', modeName: 'AgXToneMapping' },
  { label: 'Neutral', value: 'neutral', modeName: 'NeutralToneMapping' },
];

const canvasHost = ref(null);
const inspectorActive = ref(false);
const selectedGalleryIndex = ref(0);
const exposure = ref(1);
const selectedToneMapping = ref('aces-filmic');
const loadState = ref('idle');
const loadProgress = ref(0);
const errorMessage = ref('');
const imageSize = ref('');
const zoom = ref(1);
const isDragging = ref(false);
const selectedGalleryItem = computed(() => galleryItems[selectedGalleryIndex.value]);

const minZoom = 1;
const maxZoom = 8;

let renderer;
let three;
let ExrLoader;
let scene;
let camera;
let previewMesh;
let previewTexture;
let resizeObserver;
let loadSequence = 0;
let viewportAspect = 1;
let basePreviewWidth = 0;
let basePreviewHeight = 0;
let panX = 0;
let panY = 0;
let dragStart;

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

function render() {
  if (!renderer || !scene || !camera) return;
  renderer.toneMappingExposure = exposure.value;
  renderer.render(scene, camera);
}

function updateToneMapping() {
  if (!renderer || !three) return;
  const option = toneMappingOptions.find(({ value }) => value === selectedToneMapping.value);
  renderer.toneMapping = three[option?.modeName] ?? three.ACESFilmicToneMapping;
  if (previewMesh) previewMesh.material.needsUpdate = true;
  render();
}

function clamp(value, minimum, maximum) {
  return Math.min(Math.max(value, minimum), maximum);
}

function updatePreviewTransform() {
  if (!previewMesh || !previewTexture) return;
  const width = basePreviewWidth * zoom.value;
  const height = basePreviewHeight * zoom.value;
  const maxPanX = Math.max((width / 2) - viewportAspect, 0);
  const maxPanY = Math.max((height / 2) - 1, 0);

  panX = clamp(panX, -maxPanX, maxPanX);
  panY = clamp(panY, -maxPanY, maxPanY);
  previewMesh.scale.set(width, height, 1);
  previewMesh.position.set(panX, panY, 0);
  render();
}

function resetPreviewTransform() {
  zoom.value = minZoom;
  panX = 0;
  panY = 0;
  dragStart = undefined;
  isDragging.value = false;
  updatePreviewTransform();
}

function changeZoom(factor) {
  if (loadState.value !== 'ready') return;
  zoom.value = clamp(zoom.value * factor, minZoom, maxZoom);
  updatePreviewTransform();
}

function fitPreview() {
  if (!renderer || !camera || !previewMesh || !canvasHost.value) return;
  const width = Math.max(canvasHost.value.clientWidth, 1);
  const height = Math.max(canvasHost.value.clientHeight, 1);
  viewportAspect = width / height;

  camera.left = -viewportAspect;
  camera.right = viewportAspect;
  camera.top = 1;
  camera.bottom = -1;
  camera.updateProjectionMatrix();
  renderer.setSize(width, height, false);

  if (!previewTexture) {
    render();
    return;
  }

  const imageAspect = previewTexture.image.width / previewTexture.image.height;
  if (imageAspect > viewportAspect) {
    basePreviewWidth = 2 * viewportAspect;
    basePreviewHeight = (2 * viewportAspect) / imageAspect;
  } else {
    basePreviewWidth = 2 * imageAspect;
    basePreviewHeight = 2;
  }
  updatePreviewTransform();
}

function pointerPositionInScene(event) {
  const bounds = event.currentTarget.getBoundingClientRect();
  return {
    x: camera.left + ((event.clientX - bounds.left) / bounds.width) * (camera.right - camera.left),
    y: camera.top - ((event.clientY - bounds.top) / bounds.height) * (camera.top - camera.bottom),
  };
}

function handleViewerWheel(event) {
  if (loadState.value !== 'ready') return;
  const previousZoom = zoom.value;
  const nextZoom = clamp(previousZoom * Math.exp(-event.deltaY * 0.0015), minZoom, maxZoom);
  if (nextZoom === previousZoom) return;

  event.preventDefault();
  const pointer = pointerPositionInScene(event);
  const zoomRatio = nextZoom / previousZoom;
  panX = pointer.x - ((pointer.x - panX) * zoomRatio);
  panY = pointer.y - ((pointer.y - panY) * zoomRatio);
  zoom.value = nextZoom;
  updatePreviewTransform();
}

function handleViewerPointerDown(event) {
  if (event.button !== 0 || loadState.value !== 'ready') return;
  event.currentTarget.setPointerCapture(event.pointerId);
  dragStart = {
    pointerId: event.pointerId,
    clientX: event.clientX,
    clientY: event.clientY,
    panX,
    panY,
  };
  isDragging.value = true;
}

function handleViewerPointerMove(event) {
  if (!dragStart || event.pointerId !== dragStart.pointerId) return;
  const bounds = event.currentTarget.getBoundingClientRect();
  panX = dragStart.panX
    + ((event.clientX - dragStart.clientX) / bounds.width) * (camera.right - camera.left);
  panY = dragStart.panY
    - ((event.clientY - dragStart.clientY) / bounds.height) * (camera.top - camera.bottom);
  updatePreviewTransform();
}

function endViewerDrag(event) {
  if (!dragStart || event.pointerId !== dragStart.pointerId) return;
  if (event.currentTarget.hasPointerCapture(event.pointerId)) {
    event.currentTarget.releasePointerCapture(event.pointerId);
  }
  dragStart = undefined;
  isDragging.value = false;
}

function releaseInspectorResources() {
  loadSequence += 1;
  resizeObserver?.disconnect();
  previewTexture?.dispose();
  previewMesh?.geometry.dispose();
  previewMesh?.material.dispose();
  renderer?.dispose();
  renderer?.domElement.remove();
  resizeObserver = undefined;
  previewTexture = undefined;
  previewMesh = undefined;
  renderer = undefined;
  scene = undefined;
  camera = undefined;
}

function loadSelectedExr() {
  if (!renderer || !previewMesh || !ExrLoader) return;
  loadSequence += 1;
  const sequence = loadSequence;
  loadState.value = 'loading';
  loadProgress.value = 0;
  errorMessage.value = '';
  imageSize.value = '';
  resetPreviewTransform();

  new ExrLoader().load(
    selectedGalleryItem.value.exrUrl,
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

async function activateInspector() {
  if (renderer) return;
  inspectorActive.value = true;
  loadState.value = 'loading';
  await nextTick();

  try {
    const [threeModule, { EXRLoader: ExrLoaderClass }] = await Promise.all([
      import('three'),
      import('three/addons/loaders/EXRLoader.js'),
    ]);
    three = threeModule;
    ExrLoader = ExrLoaderClass;
    renderer = new three.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.outputColorSpace = three.SRGBColorSpace;
    scene = new three.Scene();
    scene.background = new three.Color(0x090b10);
    camera = new three.OrthographicCamera(-1, 1, 1, -1, 0, 1);
    previewMesh = new three.Mesh(
      new three.PlaneGeometry(1, 1),
      new three.MeshBasicMaterial({ toneMapped: true }),
    );
    scene.add(previewMesh);
    renderer.domElement.className = 'block size-full';
    canvasHost.value.appendChild(renderer.domElement);
    resizeObserver = new ResizeObserver(fitPreview);
    resizeObserver.observe(canvasHost.value);
    updateToneMapping();
    fitPreview();
    loadSelectedExr();
  } catch (error) {
    releaseInspectorResources();
    loadState.value = 'error';
    errorMessage.value = error?.message || 'WebGL is unavailable in this browser.';
  }
}

function retryInspector() {
  if (renderer) {
    loadSelectedExr();
    return;
  }
  activateInspector();
}

function handleViewerKeydown(event) {
  if (event.target !== event.currentTarget) return;
  if (event.key === 'ArrowLeft') {
    event.preventDefault();
    showPreviousGalleryItem();
  } else if (event.key === 'ArrowRight') {
    event.preventDefault();
    showNextGalleryItem();
  } else if (event.key === '+' || event.key === '=') {
    event.preventDefault();
    changeZoom(1.25);
  } else if (event.key === '-') {
    event.preventDefault();
    changeZoom(0.8);
  } else if (event.key === '0' || event.key === 'Home') {
    event.preventDefault();
    resetPreviewTransform();
  }
}

watch(exposure, render);
watch(selectedToneMapping, updateToneMapping);
watch(selectedGalleryIndex, () => {
  if (inspectorActive.value) loadSelectedExr();
}, { flush: 'sync' });

onBeforeUnmount(() => {
  releaseInspectorResources();
});
</script>

<template>
  <div class="grid gap-4 rounded-2xl border border-slate-800 bg-slate-900/70 p-4 shadow-2xl shadow-black/30 lg:grid-cols-[18rem_minmax(0,1fr)]">
    <aside class="flex flex-col gap-5 rounded-xl bg-slate-950/60 p-4">
      <div>
        <p class="text-xs font-semibold uppercase tracking-[0.18em] text-cyan-400">
          HDR inspector
        </p>
        <p class="mt-2 text-xs leading-5 text-slate-400">
          Posters load first. The selected OpenEXR downloads only after you start the inspector.
        </p>
      </div>

      <button
        v-if="!inspectorActive"
        class="rounded-lg bg-cyan-300 px-4 py-2.5 text-sm font-semibold text-slate-950 transition hover:bg-cyan-200 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-cyan-200"
        type="button"
        @click="activateInspector"
      >
        Inspect HDR OpenEXR
      </button>

      <label class="flex flex-col gap-2 text-sm font-medium text-slate-300">
        <span>Tone mapping</span>
        <select
          v-model="selectedToneMapping"
          class="rounded-lg border border-slate-700 bg-slate-900 px-3 py-2 text-slate-100 outline-none transition focus:border-cyan-400 focus:ring-2 focus:ring-cyan-400/20 disabled:cursor-not-allowed disabled:opacity-50"
          :disabled="!inspectorActive"
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
          class="accent-cyan-400 disabled:cursor-not-allowed disabled:opacity-50"
          type="range"
          min="0.1"
          max="4"
          step="0.05"
          :disabled="!inspectorActive"
        >
      </label>

      <dl class="grid grid-cols-[auto_1fr] gap-x-3 gap-y-2 border-t border-slate-800 pt-4 text-xs">
        <dt class="text-slate-500">
          Renderer
        </dt>
        <dd class="text-right text-slate-300">
          {{ galleryMetadata.renderer }}
        </dd>
        <dt class="text-slate-500">
          Pipeline
        </dt>
        <dd class="text-right text-slate-300">
          {{ galleryMetadata.pipeline }}
        </dd>
        <dt class="text-slate-500">
          Sampling
        </dt>
        <dd class="text-right text-slate-300">
          {{ galleryMetadata.sampling }}
        </dd>
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
          {{ imageSize || selectedGalleryItem.resolution }}
        </dd>
        <dt class="text-slate-500">
          Format
        </dt>
        <dd class="text-right text-slate-300">
          {{ galleryMetadata.format }}
        </dd>
      </dl>

      <p class="border-t border-slate-800 pt-4 text-[0.6875rem] leading-5 text-amber-200/80">
        {{ galleryMetadata.provenance }}
      </p>
    </aside>

    <figure
      class="overflow-hidden rounded-xl border border-slate-800 bg-slate-950 outline-none focus-visible:ring-2 focus-visible:ring-cyan-400"
      tabindex="0"
      :aria-busy="loadState === 'loading'"
      :aria-label="`Renderer capture: ${selectedGalleryItem.title}`"
      aria-keyshortcuts="ArrowLeft ArrowRight + - 0 Home"
      @keydown="handleViewerKeydown"
    >
      <div
        class="relative aspect-[8/5] overflow-hidden bg-[#090b10]"
        :class="{
          'cursor-grabbing': isDragging,
          'cursor-grab': loadState === 'ready' && !isDragging,
          'touch-none': loadState === 'ready',
          'touch-pan-y': loadState !== 'ready',
        }"
        @pointercancel="endViewerDrag"
        @pointerdown="handleViewerPointerDown"
        @pointermove="handleViewerPointerMove"
        @pointerup="endViewerDrag"
        @wheel="handleViewerWheel"
      >
        <img
          v-if="!inspectorActive"
          class="size-full object-contain"
          :src="selectedGalleryItem.posterUrl"
          :alt="`${selectedGalleryItem.title} renderer capture, ACES Filmic preview`"
          decoding="async"
        >

        <div
          v-if="!inspectorActive"
          class="absolute inset-0 grid place-items-center bg-slate-950/15 p-6"
        >
          <button
            class="rounded-xl border border-white/20 bg-slate-950/85 px-5 py-3 text-sm font-semibold text-white shadow-xl backdrop-blur transition hover:border-cyan-300 hover:text-cyan-200 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-cyan-200"
            type="button"
            @click="activateInspector"
          >
            Inspect the linear HDR source
          </button>
        </div>

        <div
          v-show="inspectorActive"
          ref="canvasHost"
          class="absolute inset-0"
        />

        <div
          v-if="inspectorActive"
          class="absolute right-3 top-3 z-20 inline-flex overflow-hidden rounded-lg border border-slate-600 bg-slate-950/85 shadow-lg backdrop-blur"
          aria-label="HDR view controls"
          @pointerdown.stop
        >
          <button
            class="grid size-9 place-items-center border-r border-slate-700 text-sm text-slate-200 transition hover:bg-slate-800 focus-visible:z-10 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-cyan-300 disabled:opacity-40"
            type="button"
            aria-label="Zoom out"
            :disabled="loadState !== 'ready' || zoom <= minZoom"
            @click="changeZoom(0.8)"
          >
            −
          </button>
          <output class="grid min-w-14 place-items-center border-r border-slate-700 px-2 text-[0.6875rem] text-slate-300">
            {{ zoom.toFixed(2) }}×
          </output>
          <button
            class="grid size-9 place-items-center border-r border-slate-700 text-sm text-slate-200 transition hover:bg-slate-800 focus-visible:z-10 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-cyan-300 disabled:opacity-40"
            type="button"
            aria-label="Zoom in"
            :disabled="loadState !== 'ready' || zoom >= maxZoom"
            @click="changeZoom(1.25)"
          >
            +
          </button>
          <button
            class="px-3 text-[0.6875rem] font-semibold text-slate-200 transition hover:bg-slate-800 focus-visible:z-10 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-cyan-300 disabled:opacity-40"
            type="button"
            :disabled="loadState !== 'ready'"
            @click="resetPreviewTransform"
          >
            Reset
          </button>
        </div>

        <div
          v-if="loadState === 'loading'"
          class="pointer-events-none absolute inset-0 z-10 grid place-items-center bg-slate-950/70 backdrop-blur-sm"
          role="status"
        >
          <div class="flex flex-col items-center gap-3 text-sm text-slate-300">
            <span class="size-8 animate-spin rounded-full border-2 border-slate-700 border-t-cyan-400" />
            <span>{{ loadProgress > 0 ? `Loading ${loadProgress}%` : 'Loading OpenEXR…' }}</span>
          </div>
        </div>

        <div
          v-if="loadState === 'error'"
          class="absolute inset-0 z-10 grid place-items-center p-6 text-center"
          role="alert"
        >
          <div class="max-w-lg rounded-xl border border-red-500/30 bg-red-950/80 p-5">
            <p class="font-semibold text-red-200">
              EXR could not be displayed
            </p>
            <p class="mt-2 break-words text-sm text-red-300/80">
              {{ errorMessage }}
            </p>
            <button
              class="mt-4 rounded-lg bg-red-400 px-4 py-2 text-sm font-semibold text-red-950 transition hover:bg-red-300 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-red-200"
              type="button"
              @click="retryInspector"
            >
              Try again
            </button>
          </div>
        </div>
      </div>

      <figcaption class="grid gap-3 border-t border-slate-800 bg-slate-950 p-3 sm:grid-cols-[minmax(0,1fr)_auto] sm:items-center sm:p-4">
        <div
          class="max-w-2xl"
          aria-atomic="true"
          aria-live="polite"
        >
          <h3 class="text-base font-semibold tracking-tight text-white">
            {{ selectedGalleryItem.title }}
          </h3>
          <p class="mt-1 text-xs leading-5 text-slate-300">
            {{ selectedGalleryItem.description }}
          </p>
          <ul
            class="mt-2 flex flex-wrap gap-1.5"
            aria-label="Visible features"
          >
            <li
              v-for="tag in selectedGalleryItem.tags"
              :key="tag"
              class="rounded-full bg-slate-800 px-2 py-1 text-[0.625rem] font-medium text-slate-300"
            >
              {{ tag }}
            </li>
          </ul>
          <p class="mt-2 text-[0.6875rem] leading-4 text-slate-400">
            {{ galleryMetadata.inspector }}.
          </p>
          <a
            class="mt-1 inline-block text-[0.6875rem] leading-4 text-slate-400 underline decoration-slate-600 underline-offset-4 transition hover:text-cyan-300"
            :href="selectedGalleryItem.sourceUrl"
            rel="noreferrer"
            target="_blank"
          >
            Asset credit: {{ selectedGalleryItem.credit }}
          </a>
        </div>

        <nav
          class="inline-flex w-fit overflow-hidden rounded-lg border border-slate-700 bg-slate-900 sm:justify-self-end"
          aria-label="Gallery pagination"
        >
          <button
            class="grid size-8 place-items-center border-r border-slate-700 text-base text-slate-300 transition hover:bg-slate-800 hover:text-cyan-200 focus-visible:z-10 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-cyan-300 disabled:cursor-wait disabled:opacity-50"
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
            class="grid size-8 place-items-center border-r border-slate-700 text-[0.6875rem] font-semibold transition focus-visible:z-10 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-cyan-300 disabled:cursor-wait disabled:opacity-50"
            :class="index === selectedGalleryIndex
              ? 'bg-cyan-300 text-slate-950'
              : 'text-slate-300 hover:bg-slate-800 hover:text-cyan-200'"
            type="button"
            :aria-label="`Show ${item.title}`"
            :aria-current="index === selectedGalleryIndex ? 'page' : undefined"
            :disabled="loadState === 'loading'"
            @click="selectGalleryItem(index)"
          >
            {{ index + 1 }}
          </button>
          <button
            class="grid size-8 place-items-center text-base text-slate-300 transition hover:bg-slate-800 hover:text-cyan-200 focus-visible:z-10 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-cyan-300 disabled:cursor-wait disabled:opacity-50"
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
