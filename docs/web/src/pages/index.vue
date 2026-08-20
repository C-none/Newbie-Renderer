<script setup>
import {
  computed,
  defineAsyncComponent,
  onBeforeUnmount,
  onMounted,
  ref,
} from 'vue';
import {
  features,
  highlights,
  projectLinks,
  projectSummary,
  roadmap,
} from '@/data/project.js';

const ExrViewer = defineAsyncComponent(() => import('@/components/ExrViewer.vue'));

const doneCount = computed(() => roadmap.filter(({ done }) => done).length);
const progressPercent = computed(() => Math.round((doneCount.value / roadmap.length) * 100));

const sections = [
  { id: 'features', label: 'Features' },
  { id: 'roadmap', label: 'Todo List' },
  { id: 'gallery', label: 'Gallery' },
];

const gallerySection = ref(null);
const galleryActive = ref(false);
let galleryObserver;

onMounted(() => {
  galleryObserver = new IntersectionObserver((entries) => {
    if (!entries.some(({ isIntersecting }) => isIntersecting)) return;
    galleryActive.value = true;
    galleryObserver.disconnect();
  }, { rootMargin: '256px' });
  galleryObserver.observe(gallerySection.value);
});

onBeforeUnmount(() => galleryObserver?.disconnect());
</script>

<template>
  <main class="min-h-screen bg-slate-950 px-4 py-10 text-slate-100 sm:px-8">
    <div class="mx-auto flex w-full max-w-7xl flex-col gap-16">
      <header class="flex flex-col gap-8">
        <nav class="flex flex-wrap items-center gap-x-6 gap-y-2 text-sm text-slate-400">
          <span class="font-semibold uppercase tracking-[0.24em] text-cyan-400">Newbie Renderer</span>
          <a
            v-for="section in sections"
            :key="section.id"
            :href="`#${section.id}`"
            class="transition hover:text-slate-100"
          >
            {{ section.label }}
          </a>
          <a
            class="ml-auto rounded-lg border border-slate-700 px-3 py-1.5 font-medium text-slate-200 transition hover:border-cyan-400 hover:text-cyan-300"
            :href="projectLinks.repository"
            rel="noreferrer"
            target="_blank"
          >
            GitHub
          </a>
        </nav>

        <div class="flex flex-col gap-5">
          <h1 class="max-w-4xl text-4xl font-semibold tracking-tight sm:text-6xl">
            {{ projectSummary.tagline }}
          </h1>
          <p class="max-w-3xl text-sm leading-7 text-slate-400 sm:text-base">
            {{ projectSummary.description }}
          </p>

          <div class="flex flex-wrap gap-3">
            <a
              class="rounded-xl bg-cyan-400 px-5 py-2.5 text-sm font-semibold text-slate-950 transition hover:bg-cyan-300"
              :href="projectLinks.repository"
              rel="noreferrer"
              target="_blank"
            >
              View source
            </a>
            <a
              class="rounded-xl border border-slate-700 px-5 py-2.5 text-sm font-semibold text-slate-200 transition hover:border-cyan-400 hover:text-cyan-300"
              :href="projectLinks.architecture"
              rel="noreferrer"
              target="_blank"
            >
              Architecture docs
            </a>
          </div>
        </div>

        <dl class="grid gap-3 sm:grid-cols-2 lg:grid-cols-4">
          <div
            v-for="highlight in highlights"
            :key="highlight.label"
            class="rounded-2xl border border-slate-800 bg-slate-900/60 p-4"
          >
            <dt class="text-xs uppercase tracking-[0.18em] text-slate-500">
              {{ highlight.label }}
            </dt>
            <dd class="mt-2 text-base font-medium text-slate-100">
              {{ highlight.value }}
            </dd>
          </div>
        </dl>
      </header>

      <section
        id="features"
        class="flex scroll-mt-8 flex-col gap-6"
      >
        <div class="flex flex-col gap-2">
          <h2 class="text-2xl font-semibold tracking-tight sm:text-3xl">
            Supported features
          </h2>
          <p class="max-w-3xl text-sm leading-6 text-slate-400">
            Capabilities already implemented in the renderer.
          </p>
        </div>

        <div class="grid gap-4 md:grid-cols-2 xl:grid-cols-3">
          <article
            v-for="feature in features"
            :key="feature.title"
            class="flex flex-col gap-3 rounded-2xl border border-slate-800 bg-slate-900/60 p-5 transition hover:border-cyan-400/40"
          >
            <h3 class="text-lg font-semibold text-slate-100">
              {{ feature.title }}
            </h3>
            <p class="text-sm leading-6 text-slate-400">
              {{ feature.summary }}
            </p>
            <ul class="mt-auto flex flex-wrap gap-2 pt-1">
              <li
                v-for="tag in feature.tags"
                :key="tag"
                class="rounded-full border border-slate-700 px-2.5 py-1 text-xs text-slate-300"
              >
                {{ tag }}
              </li>
            </ul>
          </article>
        </div>
      </section>

      <section
        id="roadmap"
        class="flex scroll-mt-8 flex-col gap-6"
      >
        <div class="flex flex-col gap-3">
          <h2 class="text-2xl font-semibold tracking-tight sm:text-3xl">
            Todo list
          </h2>
          <p class="max-w-3xl text-sm leading-6 text-slate-400">
            {{ doneCount }} of {{ roadmap.length }} items completed.
          </p>
          <div class="h-2 w-full max-w-md overflow-hidden rounded-full bg-slate-800">
            <div
              class="h-full rounded-full bg-cyan-400"
              :style="{ width: `${progressPercent}%` }"
            />
          </div>
        </div>

        <ol class="flex flex-col gap-3">
          <li
            v-for="(item, index) in roadmap"
            :key="item.title"
            class="flex gap-4 rounded-2xl border border-slate-800 bg-slate-900/60 p-4"
            :class="item.done ? 'border-cyan-400/30' : ''"
          >
            <span
              class="mt-0.5 grid size-6 shrink-0 place-items-center rounded-full text-xs font-semibold"
              :class="item.done ? 'bg-cyan-400 text-slate-950' : 'border border-slate-700 text-slate-500'"
            >
              {{ item.done ? '✓' : index + 1 }}
            </span>
            <div class="flex flex-col gap-1">
              <p
                class="text-sm leading-6"
                :class="item.done ? 'text-slate-200' : 'text-slate-300'"
              >
                {{ item.title }}
              </p>
              <a
                v-if="item.reference"
                class="w-fit text-xs text-cyan-400 underline-offset-4 transition hover:underline"
                :href="item.reference.url"
                rel="noreferrer"
                target="_blank"
              >
                Reference: {{ item.reference.label }}
              </a>
            </div>
          </li>
        </ol>
      </section>

      <section
        id="gallery"
        ref="gallerySection"
        class="flex scroll-mt-8 flex-col gap-6"
      >
        <div class="flex flex-col gap-2">
          <h2 class="text-2xl font-semibold tracking-tight sm:text-3xl">
            Gallery
          </h2>
          <p class="max-w-3xl text-sm leading-6 text-slate-400">
            Browse renderer captures in OpenEXR with on-demand decoding and selectable tone mapping.
          </p>
        </div>

        <ExrViewer v-if="galleryActive" />
        <p
          v-else
          class="grid min-h-[30rem] place-items-center rounded-2xl border border-slate-800 bg-slate-900/60 text-sm text-slate-500 lg:min-h-[55vh]"
        >
          The gallery and its first EXR load when this section becomes visible.
        </p>
      </section>

      <footer class="border-t border-slate-800 pt-6 text-sm text-slate-500">
        <a
          class="transition hover:text-cyan-300"
          :href="projectLinks.readme"
          rel="noreferrer"
          target="_blank"
        >
          {{ projectSummary.name }} on GitHub
        </a>
      </footer>
    </div>
  </main>
</template>
