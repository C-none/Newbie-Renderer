<script setup>
import {
  computed,
  defineAsyncComponent,
  onBeforeUnmount,
  onMounted,
  ref,
} from 'vue';
import {
  architectureLayers,
  caseStudies,
  features,
  highlights,
  projectLinks,
  projectSummary,
  roadmapGroups,
} from '@/data/project.js';
import heroPosterUrl from '@/assets/gallery/chess.png';
import asyncComputeProfileUrl from '../../assets/img/async compute profile.png';

const ExrViewer = defineAsyncComponent(() => import('@/components/ExrViewer.vue'));

const sections = [
  { id: 'gallery', label: 'Showcase' },
  { id: 'capabilities', label: 'Capabilities' },
  { id: 'architecture', label: 'Architecture' },
  { id: 'roadmap', label: 'Roadmap' },
];

const statusLabels = {
  implemented: 'Implemented',
  experimental: 'Experimental',
  next: 'Next',
  research: 'Research',
};

const statusClasses = {
  implemented: 'border-emerald-400/40 bg-emerald-400/10 text-emerald-200',
  experimental: 'border-amber-400/40 bg-amber-400/10 text-amber-200',
  next: 'border-cyan-400/40 bg-cyan-400/10 text-cyan-200',
  research: 'border-violet-400/40 bg-violet-400/10 text-violet-200',
};

const expandedFeatureTitles = ref(new Set());
const expandableFeatures = computed(() => features.filter(({ details }) => details?.length));
const hasExpandedFeatures = computed(() => expandedFeatureTitles.value.size > 0);

function featureIsExpanded(title) {
  return expandedFeatureTitles.value.has(title);
}

function toggleFeatureDetails(title) {
  const nextExpandedTitles = new Set(expandedFeatureTitles.value);
  if (nextExpandedTitles.has(title)) {
    nextExpandedTitles.delete(title);
  } else {
    nextExpandedTitles.add(title);
  }
  expandedFeatureTitles.value = nextExpandedTitles;
}

function toggleAllFeatureDetails() {
  expandedFeatureTitles.value = hasExpandedFeatures.value
    ? new Set()
    : new Set(expandableFeatures.value.map(({ title }) => title));
}

function featureSummarySegments({ summary, summaryEmphasis = [] }) {
  const emphasizedRanges = summaryEmphasis
    .map((phrase) => {
      const start = summary.indexOf(phrase);
      return { start, end: start + phrase.length };
    })
    .filter(({ start }) => start >= 0)
    .sort((lhs, rhs) => lhs.start - rhs.start);
  const segments = [];
  let cursor = 0;

  emphasizedRanges.forEach(({ start, end }) => {
    if (start < cursor) return;
    if (start > cursor) segments.push({ text: summary.slice(cursor, start), emphasized: false });
    segments.push({ text: summary.slice(start, end), emphasized: true });
    cursor = end;
  });
  if (cursor < summary.length) segments.push({ text: summary.slice(cursor), emphasized: false });
  return segments;
}

const gallerySection = ref(null);
const galleryActive = ref(false);
let galleryObserver;

onMounted(() => {
  if (!('IntersectionObserver' in window)) {
    galleryActive.value = true;
    return;
  }

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
  <a
    class="fixed left-4 top-4 z-50 -translate-y-24 rounded-lg bg-cyan-300 px-4 py-2 font-semibold text-slate-950 transition focus:translate-y-0"
    href="#primary-content"
  >
    Skip to content
  </a>
  <main
    id="content"
    class="min-h-screen bg-slate-950 px-4 pb-10 text-slate-100 sm:px-8"
  >
    <div class="mx-auto flex w-full max-w-7xl flex-col gap-20">
      <header class="flex flex-col gap-10">
        <nav class="-mx-4 flex flex-wrap items-center gap-x-5 gap-y-2 border-b border-slate-800/80 bg-slate-950/90 p-4 text-sm text-slate-400 sm:-mx-8 sm:px-8">
          <a
            class="font-semibold uppercase tracking-[0.22em] text-cyan-300"
            href="#content"
          >
            Newbie Renderer
          </a>
          <a
            v-for="section in sections"
            :key="section.id"
            :href="`#${section.id}`"
            class="transition hover:text-slate-100 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-cyan-300"
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

        <div
          id="primary-content"
          class="grid items-center gap-8 lg:grid-cols-[minmax(0,1fr)_minmax(24rem,0.82fr)]"
          tabindex="-1"
        >
          <div class="flex flex-col items-start gap-6">
            <p class="text-xs font-semibold uppercase tracking-[0.28em] text-cyan-300">
              {{ projectSummary.eyebrow }}
            </p>
            <h1 class="max-w-4xl text-4xl font-semibold leading-tight tracking-tight sm:text-6xl">
              {{ projectSummary.tagline }}
            </h1>
            <p class="max-w-3xl text-sm leading-7 text-slate-300 sm:text-base">
              {{ projectSummary.description }}
            </p>

            <p class="max-w-3xl rounded-xl border border-amber-400/30 bg-amber-400/10 px-4 py-3 text-sm font-medium leading-6 text-amber-100">
              <span
                class="mr-2"
                aria-hidden="true"
              >⚠</span>{{ projectSummary.support }}
            </p>

            <div class="flex flex-wrap gap-3">
              <a
                class="rounded-xl bg-cyan-300 px-5 py-2.5 text-sm font-semibold text-slate-950 transition hover:bg-cyan-200 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-cyan-200"
                href="#gallery"
              >
                Explore renderer captures
              </a>
              <a
                class="rounded-xl border border-slate-700 px-5 py-2.5 text-sm font-semibold text-slate-200 transition hover:border-cyan-400 hover:text-cyan-300 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-cyan-300"
                :href="projectLinks.repository"
                rel="noreferrer"
                target="_blank"
              >
                View source ↗
              </a>
            </div>
          </div>

          <figure class="overflow-hidden rounded-2xl border border-slate-700 bg-slate-900 shadow-2xl shadow-cyan-950/30">
            <img
              class="aspect-[8/5] w-full object-cover"
              :src="heroPosterUrl"
              alt="Chess scene path-traced by Newbie Renderer"
              fetchpriority="high"
            >
            <figcaption class="flex items-center justify-between gap-4 border-t border-slate-800 px-4 py-3 text-xs text-slate-400">
              <span>Newbie Renderer capture · ACES Filmic poster</span>
              <a
                class="shrink-0 text-cyan-300 underline-offset-4 hover:underline"
                href="#gallery"
              >
                Inspect HDR
              </a>
            </figcaption>
          </figure>
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
        id="gallery"
        ref="gallerySection"
        class="flex scroll-mt-8 flex-col gap-6"
      >
        <div class="flex flex-col gap-2">
          <h2 class="text-2xl font-semibold tracking-tight sm:text-3xl">
            Renderer showcase
          </h2>
          <p class="max-w-3xl text-sm leading-6 text-slate-400">
            Real project captures arrive as lightweight posters. Start the inspector to download the selected linear HDR
            OpenEXR, then compare tone mapping, exposure, zoom, and fine detail in place.
          </p>
        </div>

        <ExrViewer v-if="galleryActive" />
        <p
          v-else
          class="grid min-h-[30rem] place-items-center rounded-2xl border border-slate-800 bg-slate-900/60 text-sm text-slate-500 lg:min-h-[55vh]"
        >
          The gallery module and first poster load when this section becomes visible.
        </p>
      </section>

      <section
        class="flex flex-col gap-6"
        aria-labelledby="case-studies-title"
      >
        <div class="flex flex-col gap-2">
          <p class="text-xs font-semibold uppercase tracking-[0.22em] text-cyan-300">
            What the code demonstrates
          </p>
          <h2
            id="case-studies-title"
            class="text-2xl font-semibold tracking-tight sm:text-3xl"
          >
            Three concrete engineering outcomes
          </h2>
          <p class="max-w-3xl text-sm leading-6 text-slate-400">
            Expand a card for implementation details and code evidence. Active research modules also show what works today and
            what remains in development.
          </p>
        </div>

        <div class="grid gap-4 lg:grid-cols-3">
          <details
            v-for="caseStudy in caseStudies"
            :key="caseStudy.title"
            class="group rounded-2xl border border-slate-800 bg-slate-900/60 p-5 open:border-cyan-400/30 lg:open:col-span-3"
          >
            <summary class="cursor-pointer list-none focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-cyan-300">
              <span class="flex items-start justify-between gap-4">
                <span>
                  <span class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-500">
                    {{ caseStudy.kicker }}
                  </span>
                  <span class="mt-3 block text-lg font-semibold text-slate-100">
                    {{ caseStudy.title }}
                  </span>
                </span>
                <span
                  v-if="caseStudy.status !== 'implemented'"
                  class="shrink-0 rounded-full border px-2.5 py-1 text-[0.6875rem] font-semibold"
                  :class="statusClasses[caseStudy.status]"
                >
                  {{ statusLabels[caseStudy.status] }}
                </span>
              </span>
              <span class="mt-3 block text-sm leading-6 text-slate-400">
                {{ caseStudy.summary }}
              </span>
              <span class="mt-4 inline-flex items-center gap-2 text-xs font-semibold text-cyan-300">
                <span class="group-open:hidden">Open case study</span>
                <span class="hidden group-open:inline">Close case study</span>
                <span
                  class="transition-transform group-open:rotate-180"
                  aria-hidden="true"
                >⌄</span>
              </span>
            </summary>

            <div class="mt-5 grid gap-5 border-t border-slate-800 pt-5 lg:grid-cols-[minmax(0,1fr)_20rem]">
              <div>
                <h3 class="text-sm font-semibold text-slate-100">
                  Implementation
                </h3>
                <ul class="mt-3 flex flex-col gap-2 text-sm leading-6 text-slate-300">
                  <li
                    v-for="detail in caseStudy.details"
                    :key="detail"
                    class="flex gap-3"
                  >
                    <span
                      class="mt-2 size-1.5 shrink-0 rounded-full bg-cyan-300"
                      aria-hidden="true"
                    />
                    <span>{{ detail }}</span>
                  </li>
                </ul>
              </div>

              <aside class="rounded-xl border border-slate-800 bg-slate-950/50 p-4">
                <div
                  v-if="caseStudy.status !== 'implemented' && caseStudy.development?.length"
                  class="mb-4 rounded-lg border border-amber-400/20 bg-amber-400/5 p-3"
                >
                  <h3 class="text-xs font-semibold uppercase tracking-[0.16em] text-amber-200">
                    Current implementation
                  </h3>
                  <dl class="mt-3 grid gap-3">
                    <div
                      v-for="stage in caseStudy.development"
                      :key="stage.label"
                    >
                      <dt class="text-xs font-semibold text-amber-100">
                        {{ stage.label }}
                      </dt>
                      <dd class="mt-1 text-xs leading-5 text-amber-100/80">
                        {{ stage.summary }}
                      </dd>
                    </div>
                  </dl>
                </div>
                <h3 class="text-xs font-semibold uppercase tracking-[0.16em] text-slate-300">
                  Evidence
                </h3>
                <ul class="mt-2 flex flex-col gap-2">
                  <li
                    v-for="evidence in caseStudy.evidence"
                    :key="evidence.url"
                  >
                    <a
                      class="text-xs text-cyan-300 underline-offset-4 hover:underline"
                      :href="evidence.url"
                      rel="noreferrer"
                      target="_blank"
                    >
                      {{ evidence.label }} ↗
                    </a>
                  </li>
                </ul>
              </aside>

              <figure
                v-if="caseStudy.visualization === 'async-profile'"
                class="overflow-hidden rounded-xl border border-slate-700 bg-slate-950 lg:col-span-2"
              >
                <img
                  class="block h-auto w-full"
                  :src="asyncComputeProfileUrl"
                  alt="GPU profile showing path tracing on graphics overlapping DLSS Ray Reconstruction on compute"
                  decoding="async"
                  loading="lazy"
                >
                <figcaption class="border-t border-slate-700 p-3 text-xs leading-5 text-slate-400">
                  Captured overlap is scheduling evidence, not a portable performance benchmark.
                </figcaption>
              </figure>
            </div>
          </details>
        </div>
      </section>

      <section
        id="capabilities"
        class="flex scroll-mt-8 flex-col gap-6"
      >
        <div class="flex flex-col gap-4 sm:flex-row sm:items-end sm:justify-between">
          <div class="flex flex-col gap-2">
            <h2 class="text-2xl font-semibold tracking-tight sm:text-3xl">
              Capability map
            </h2>
            <p class="max-w-3xl text-sm leading-6 text-slate-400">
              Production-path systems and active research modules are labeled separately. Expand any capability for implementation
              details and evidence; only work in development includes a progress panel.
            </p>
          </div>

          <button
            v-if="expandableFeatures.length"
            class="w-fit rounded-lg border border-slate-700 px-3 py-2 text-xs font-semibold text-slate-300 transition hover:border-cyan-400/70 hover:text-cyan-300 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-cyan-300"
            type="button"
            @click="toggleAllFeatureDetails"
          >
            {{ hasExpandedFeatures ? 'Collapse all' : 'Expand all' }}
          </button>
        </div>

        <div class="grid gap-3">
          <article
            v-for="(feature, index) in features"
            :key="feature.title"
            class="min-w-0 rounded-2xl border border-slate-800 bg-slate-900/60 p-5 transition hover:border-cyan-400/40"
          >
            <div class="flex flex-col gap-3 sm:flex-row sm:items-start">
              <div class="min-w-0 flex-1">
                <div class="flex flex-wrap items-center gap-2">
                  <span class="text-xs font-semibold uppercase tracking-[0.14em] text-slate-500">
                    {{ feature.category }}
                  </span>
                  <span
                    v-if="feature.status !== 'implemented'"
                    class="rounded-full border px-2.5 py-1 text-[0.6875rem] font-semibold"
                    :class="statusClasses[feature.status]"
                  >
                    {{ statusLabels[feature.status] }}
                  </span>
                </div>
                <h3 class="mt-2 text-lg font-semibold text-slate-100">
                  {{ feature.title }}
                </h3>
                <ul class="flex flex-wrap gap-2">
                  <li
                    v-for="tag in feature.tags"
                    :key="tag"
                    class="mt-3 rounded-full border border-slate-700 px-2.5 py-1 text-xs text-slate-300"
                  >
                    {{ tag }}
                  </li>
                </ul>
              </div>

              <button
                v-if="feature.details?.length"
                class="flex w-fit shrink-0 items-center gap-2 rounded-lg border border-slate-700 px-3 py-1.5 text-xs font-semibold text-slate-300 transition hover:border-cyan-400/70 hover:text-cyan-300 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-cyan-300"
                type="button"
                :aria-controls="`feature-details-${index}`"
                :aria-expanded="featureIsExpanded(feature.title)"
                @click="toggleFeatureDetails(feature.title)"
              >
                {{ featureIsExpanded(feature.title) ? 'Hide details' : 'View details' }}
                <span
                  class="text-sm transition-transform"
                  :class="featureIsExpanded(feature.title) ? 'rotate-180' : ''"
                  aria-hidden="true"
                >⌄</span>
              </button>
            </div>

            <p class="mt-3 text-sm leading-6 text-slate-400">
              <component
                :is="segment.emphasized ? 'strong' : 'span'"
                v-for="(segment, segmentIndex) in featureSummarySegments(feature)"
                :key="`${feature.title}-${segmentIndex}`"
                :class="segment.emphasized ? 'font-semibold text-slate-200' : ''"
              >
                {{ segment.text }}
              </component>
            </p>

            <div
              v-if="feature.details?.length && featureIsExpanded(feature.title)"
              :id="`feature-details-${index}`"
              class="mt-4 border-t border-slate-800 pt-4"
            >
              <figure
                v-if="feature.visualization === 'variant-system'"
                class="mb-5 min-w-0 max-w-full rounded-xl border border-slate-700 bg-slate-950/70 p-4"
              >
                <figcaption>
                  <h4 class="text-sm font-semibold text-slate-100">
                    Compile order and reusable results
                  </h4>
                  <p class="mt-1 text-xs leading-5 text-slate-400">
                    Read the numbered stages from top to bottom. Double outlines identify artifacts the renderer can restore;
                    single outlines are generated for the current request.
                  </p>
                </figcaption>

                <div class="mt-4 min-w-0 max-w-full overflow-x-auto pb-2">
                  <table
                    class="w-full min-w-[62rem] table-fixed border-separate border-spacing-2 text-center text-xs"
                    aria-label="Shader variant compilation stages and reusable artifacts"
                  >
                    <thead class="text-[0.6875rem] uppercase tracking-[0.12em] text-slate-500">
                      <tr>
                        <th class="w-28 px-2 py-1 font-medium">
                          Compile stage
                        </th>
                        <th class="w-48 px-2 py-1 font-medium">
                          Shared library
                        </th>
                        <th class="w-32 px-2 py-1 font-medium">
                          Variant 0
                        </th>
                        <th class="w-32 px-2 py-1 font-medium">
                          Variant 1
                        </th>
                        <th class="w-32 px-2 py-1 font-medium">
                          Variant 2
                        </th>
                        <th class="w-60 px-2 py-1 font-medium">
                          Reuse / acceleration
                        </th>
                      </tr>
                    </thead>
                    <tbody class="text-slate-300 [&_td]:align-middle [&_th]:align-middle">
                      <tr>
                        <th class="px-2 py-3 font-medium text-cyan-300">
                          <span class="block text-[0.625rem] tracking-[0.18em] text-slate-600">01</span>
                          <span class="mt-1 block">Slang code</span>
                        </th>
                        <td class="rounded-lg border border-cyan-400/40 bg-cyan-400/10 p-3">
                          Modules, interfaces, generics, and the sole entry point
                        </td>
                        <td class="rounded-lg border border-violet-400/40 bg-violet-400/10 p-3">
                          Values + concrete types 0
                        </td>
                        <td class="rounded-lg border border-violet-400/40 bg-violet-400/10 p-3">
                          Values + concrete types 1
                        </td>
                        <td class="rounded-lg border border-violet-400/40 bg-violet-400/10 p-3">
                          Values + concrete types 2
                        </td>
                        <td class="rounded-lg bg-slate-900 p-3 leading-5 text-slate-400">
                          Modern Slang types keep behavior composable without cloned preprocessor permutations.
                        </td>
                      </tr>
                      <tr>
                        <th class="px-2 py-3 font-medium text-cyan-300">
                          <span class="block text-[0.625rem] tracking-[0.18em] text-slate-600">02</span>
                          <span class="mt-1 block">Slang compilation</span>
                        </th>
                        <td class="rounded-xl border border-emerald-300/70 p-1">
                          <div class="flex min-h-20 flex-col items-center justify-center rounded-lg border border-emerald-400/40 bg-emerald-400/10 p-3 text-center">
                            <p class="font-semibold text-emerald-200">
                              Reusable SlangIR
                            </p>
                            <p class="mt-1 text-[0.6875rem] text-emerald-300/80">
                              .slang-module
                            </p>
                          </div>
                        </td>
                        <td class="rounded-lg bg-violet-400/10 p-3 text-violet-200">
                          Small variant IR 0
                        </td>
                        <td class="rounded-lg bg-violet-400/10 p-3 text-violet-200">
                          Small variant IR 1
                        </td>
                        <td class="rounded-lg bg-violet-400/10 p-3 text-violet-200">
                          Small variant IR 2
                        </td>
                        <td class="rounded-lg bg-slate-900 p-3 leading-5 text-slate-400">
                          A validated <span class="font-mono text-emerald-300">.slang-module</span> restores parsing, type
                          checking, and lowering for the shared library across variants and launches.
                        </td>
                      </tr>
                      <tr>
                        <th class="px-2 py-3 font-medium text-cyan-300">
                          <span class="block text-[0.625rem] tracking-[0.18em] text-slate-600">03</span>
                          <span class="mt-1 block">Slang linking</span>
                        </th>
                        <td class="rounded-lg bg-sky-400/10 p-3 text-sky-200">
                          Root program + entry
                        </td>
                        <td class="rounded-lg border border-blue-400/40 bg-blue-400/10 p-3 text-blue-200">
                          Linked program 0
                        </td>
                        <td class="rounded-lg border border-blue-400/40 bg-blue-400/10 p-3 text-blue-200">
                          Linked program 1
                        </td>
                        <td class="rounded-lg border border-blue-400/40 bg-blue-400/10 p-3 text-blue-200">
                          Linked program 2
                        </td>
                        <td class="rounded-lg bg-slate-900 p-3 leading-5 text-slate-400">
                          The current renderer creates the specialized linked result for the request; it does not persist or
                          reuse linked-program objects.
                        </td>
                      </tr>
                      <tr>
                        <th class="px-2 py-3 font-medium text-cyan-300">
                          <span class="block text-[0.625rem] tracking-[0.18em] text-slate-600">04</span>
                          <span class="mt-1 block">Target code</span>
                        </th>
                        <td class="rounded-lg bg-slate-900 p-3 text-slate-400">
                          Authoritative specialized entry hash
                        </td>
                        <td class="rounded-xl border border-teal-300/70 p-1">
                          <div class="flex min-h-20 flex-col items-center justify-center rounded-lg border border-teal-400/40 bg-teal-400/10 p-3 text-center text-teal-200">
                            <p>SPIR-V 0</p>
                            <p class="mt-1 text-[0.6875rem] text-teal-300/80">
                              Disk cache
                            </p>
                          </div>
                        </td>
                        <td class="rounded-xl border border-teal-300/70 p-1">
                          <div class="flex min-h-20 flex-col items-center justify-center rounded-lg border border-teal-400/40 bg-teal-400/10 p-3 text-center text-teal-200">
                            <p>SPIR-V 1</p>
                            <p class="mt-1 text-[0.6875rem] text-teal-300/80">
                              Disk cache
                            </p>
                          </div>
                        </td>
                        <td class="rounded-xl border border-teal-300/70 p-1">
                          <div class="flex min-h-20 flex-col items-center justify-center rounded-lg border border-teal-400/40 bg-teal-400/10 p-3 text-center text-teal-200">
                            <p>SPIR-V 2</p>
                            <p class="mt-1 text-[0.6875rem] text-teal-300/80">
                              Disk cache
                            </p>
                          </div>
                        </td>
                        <td class="rounded-lg bg-slate-900 p-3 leading-5 text-slate-400">
                          Slang's authoritative entry-point hash addresses the SPIR-V artifact <span class="font-semibold text-teal-300">saved in the on-disk shader cache</span>, bypassing target code generation across processes.
                        </td>
                      </tr>
                      <tr>
                        <th class="px-2 py-3 font-medium text-cyan-300">
                          <span class="block text-[0.625rem] tracking-[0.18em] text-slate-600">05</span>
                          <span class="mt-1 block">Pipeline binary</span>
                        </th>
                        <td class="rounded-lg bg-slate-900 p-3 text-slate-400">
                          Driver key + complete PSO content fingerprint
                        </td>
                        <td class="rounded-xl border border-fuchsia-300/70 p-1">
                          <div class="flex min-h-20 items-center justify-center rounded-lg border border-fuchsia-400/40 bg-fuchsia-400/10 p-3 text-center text-fuchsia-200">
                            Opaque PSO binary 0
                          </div>
                        </td>
                        <td class="rounded-xl border border-fuchsia-300/70 p-1">
                          <div class="flex min-h-20 items-center justify-center rounded-lg border border-fuchsia-400/40 bg-fuchsia-400/10 p-3 text-center text-fuchsia-200">
                            Opaque PSO binary 1
                          </div>
                        </td>
                        <td class="rounded-xl border border-fuchsia-300/70 p-1">
                          <div class="flex min-h-20 items-center justify-center rounded-lg border border-fuchsia-400/40 bg-fuchsia-400/10 p-3 text-center text-fuchsia-200">
                            Opaque PSO binary 2
                          </div>
                        </td>
                        <td class="rounded-lg bg-slate-900 p-3 leading-5 text-slate-400">
                          <span class="font-mono text-fuchsia-300">VK_KHR_pipeline_binary</span> restores the driver's ordered
                          binary sequence and recreates a matching PSO without compilation.
                        </td>
                      </tr>
                      <tr>
                        <th class="px-2 py-3 font-medium text-cyan-300">
                          <span class="block text-[0.625rem] tracking-[0.18em] text-slate-600">06</span>
                          <span class="mt-1 block">Driver executable</span>
                        </th>
                        <td class="rounded-lg bg-slate-900 p-3 text-slate-400">
                          Implementation-defined final pipeline
                        </td>
                        <td class="rounded-lg border border-amber-400/40 bg-amber-400/10 p-3 text-amber-200">
                          GPU executable 0
                        </td>
                        <td class="rounded-lg border border-amber-400/40 bg-amber-400/10 p-3 text-amber-200">
                          GPU executable 1
                        </td>
                        <td class="rounded-lg border border-amber-400/40 bg-amber-400/10 p-3 text-amber-200">
                          GPU executable 2
                        </td>
                        <td class="rounded-lg bg-slate-900 p-3 leading-5 text-slate-400">
                          GPU assembly is not a project-visible cache artifact. The reusable artifact is the opaque pipeline
                          binary; whether it contains ISA is driver-defined.
                        </td>
                      </tr>
                    </tbody>
                  </table>
                </div>

                <div class="mt-3 flex flex-wrap gap-x-4 gap-y-2 text-[0.6875rem] text-slate-400">
                  <span class="inline-flex items-center"><i class="mr-1.5 inline-flex size-3 items-center justify-center rounded border border-slate-300"><i class="size-1.5 rounded-sm border border-slate-400" /></i>Double outline = reusable artifact</span>
                  <span><i class="mr-1.5 inline-block size-2 rounded-full bg-emerald-400" />Reusable front-end work</span>
                  <span><i class="mr-1.5 inline-block size-2 rounded-full bg-blue-400" />Per-request linked program</span>
                  <span><i class="mr-1.5 inline-block size-2 rounded-full bg-teal-400" />Reusable target code</span>
                  <span><i class="mr-1.5 inline-block size-2 rounded-full bg-violet-400" />Variant-specific input</span>
                  <span><i class="mr-1.5 inline-block size-2 rounded-full bg-fuchsia-400" />Reusable opaque PSO binary</span>
                  <span><i class="mr-1.5 inline-block size-2 rounded-full bg-amber-400" />Driver-defined executable</span>
                </div>
              </figure>

              <ul class="flex flex-col gap-2 text-sm leading-6 text-slate-300">
                <li
                  v-for="detail in feature.details"
                  :key="detail"
                  class="flex gap-3"
                >
                  <span
                    class="mt-2 size-1.5 shrink-0 rounded-full bg-cyan-400/70"
                    aria-hidden="true"
                  />
                  <span>{{ detail }}</span>
                </li>
              </ul>

              <div
                v-if="feature.status !== 'implemented' && feature.development?.length"
                class="mt-4 rounded-xl border border-amber-400/20 bg-amber-400/5 p-4"
              >
                <h4 class="text-xs font-semibold uppercase tracking-[0.14em] text-amber-200">
                  Current implementation
                </h4>
                <dl class="mt-3 grid gap-3 sm:grid-cols-2">
                  <div
                    v-for="stage in feature.development"
                    :key="stage.label"
                  >
                    <dt class="text-xs font-semibold text-amber-100">
                      {{ stage.label }}
                    </dt>
                    <dd class="mt-1 text-xs leading-5 text-amber-100/80">
                      {{ stage.summary }}
                    </dd>
                  </div>
                </dl>
              </div>

              <ul
                v-if="feature.references?.length"
                class="mt-4 flex flex-wrap gap-x-4 gap-y-2"
                aria-label="Feature references"
              >
                <li
                  v-for="reference in feature.references"
                  :key="reference.url"
                >
                  <a
                    class="text-xs text-cyan-400 underline-offset-4 transition hover:underline"
                    :href="reference.url"
                    rel="noreferrer"
                    target="_blank"
                  >
                    {{ reference.label }} ↗
                  </a>
                </li>
              </ul>

              <ul
                v-if="feature.evidence?.length"
                class="mt-4 flex flex-wrap gap-x-4 gap-y-2"
                aria-label="Implementation evidence"
              >
                <li
                  v-for="evidence in feature.evidence"
                  :key="evidence.url"
                >
                  <a
                    class="text-xs text-cyan-300 underline-offset-4 transition hover:underline"
                    :href="evidence.url"
                    rel="noreferrer"
                    target="_blank"
                  >
                    Evidence: {{ evidence.label }} ↗
                  </a>
                </li>
              </ul>

              <div
                v-if="feature.visualization === 'cross-frame-async'"
                class="mt-5 grid min-w-0 gap-4"
              >
                <figure class="min-w-0 max-w-full rounded-xl border border-slate-700 bg-slate-950/70 p-4">
                  <figcaption>
                    <h4 class="text-sm font-semibold text-slate-100">
                      Cross-frame pipeline timeline
                    </h4>
                    <p class="mt-1 text-xs leading-5 text-slate-400">
                      Schematic—not to scale. Independent frame slots let current graphics work run while older reconstruction,
                      and presentation work remains on the compute queue.
                    </p>
                  </figcaption>

                  <div class="mt-4 min-w-0 max-w-full overflow-x-auto pb-2">
                    <div class="grid min-w-[42rem] grid-cols-[6.5rem_minmax(0,1fr)] gap-3">
                      <span />
                      <div class="grid grid-cols-4 text-[0.6875rem] text-slate-500">
                        <span>t0</span>
                        <span>t1</span>
                        <span>t2</span>
                        <span class="text-right">t3</span>
                      </div>

                      <p class="self-center text-xs font-medium text-cyan-300">
                        Graphics
                      </p>
                      <div class="relative h-12 overflow-hidden rounded-lg bg-slate-900">
                        <div class="absolute inset-y-1 left-[22%] flex w-[46%] items-center justify-center rounded-md border border-cyan-400/50 bg-cyan-400/20 px-3 text-xs font-semibold text-cyan-100">
                          Frame n · RT → UI
                        </div>
                      </div>

                      <p class="self-center text-xs font-medium text-violet-300">
                        Compute
                      </p>
                      <div class="relative h-12 overflow-hidden rounded-lg bg-slate-900">
                        <div class="absolute inset-y-1 left-0 flex w-[52%] items-center justify-center rounded-md border border-violet-400/50 bg-violet-400/20 px-3 text-xs font-semibold text-violet-100">
                          Frame n−2 · DLSS → Present
                        </div>
                        <div class="absolute inset-y-1 left-[54%] flex w-[46%] items-center justify-center rounded-md border border-fuchsia-400/50 bg-fuchsia-400/20 px-3 text-xs font-semibold text-fuchsia-100">
                          Frame n−1 · DLSS → Present
                        </div>
                      </div>
                    </div>
                  </div>

                  <p class="mt-2 text-xs leading-5 text-slate-400">
                    Per-queue timeline semaphores preserve actual dependencies at the consumer stage. They do not impose a
                    global frame barrier, so the cyan Frame n block can overlap the violet/fuchsia tails of older frames.
                  </p>
                </figure>
              </div>
            </div>
          </article>
        </div>
      </section>

      <section
        id="architecture"
        class="flex scroll-mt-20 flex-col gap-6"
      >
        <div class="flex flex-col gap-2">
          <p class="text-xs font-semibold uppercase tracking-[0.22em] text-cyan-300">
            System map
          </p>
          <h2 class="text-2xl font-semibold tracking-tight sm:text-3xl">
            Architecture on one page
          </h2>
          <p class="max-w-3xl text-sm leading-6 text-slate-400">
            Stable system responsibilities are visible at a glance. Open each layer for its current data flow.
          </p>
        </div>

        <ol
          class="grid gap-3 lg:grid-cols-5"
          aria-label="Renderer architecture layers"
        >
          <li
            v-for="(layer, index) in architectureLayers"
            :key="layer.title"
            class="relative rounded-xl border border-slate-700 bg-slate-900/70 p-4"
          >
            <span class="text-xs font-semibold text-cyan-300">0{{ index + 1 }}</span>
            <p class="mt-2 text-sm font-semibold text-slate-100">
              {{ layer.title }}
            </p>
            <span
              v-if="index < architectureLayers.length - 1"
              class="absolute -right-3 top-1/2 z-10 hidden -translate-y-1/2 text-cyan-400 lg:block"
              aria-hidden="true"
            >→</span>
          </li>
        </ol>

        <div class="grid gap-3 lg:grid-cols-2">
          <details
            v-for="layer in architectureLayers"
            :key="layer.title"
            class="group rounded-xl border border-slate-800 bg-slate-900/60 p-4 open:border-cyan-400/30"
          >
            <summary class="cursor-pointer list-none focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-cyan-300">
              <span class="flex items-center justify-between gap-4">
                <span class="font-semibold text-slate-100">{{ layer.title }}</span>
                <span
                  class="transition-transform group-open:rotate-180"
                  aria-hidden="true"
                >⌄</span>
              </span>
              <span class="mt-2 block text-sm leading-6 text-slate-400">{{ layer.summary }}</span>
            </summary>
            <ul class="mt-4 flex flex-col gap-2 border-t border-slate-800 pt-4 text-sm leading-6 text-slate-300">
              <li
                v-for="detail in layer.details"
                :key="detail"
                class="flex gap-3"
              >
                <span
                  class="mt-2 size-1.5 shrink-0 rounded-full bg-cyan-300"
                  aria-hidden="true"
                />
                <span>{{ detail }}</span>
              </li>
            </ul>
          </details>
        </div>
      </section>

      <section
        id="roadmap"
        class="flex scroll-mt-20 flex-col gap-6"
      >
        <div class="flex flex-col gap-2">
          <p class="text-xs font-semibold uppercase tracking-[0.22em] text-cyan-300">
            Status, not percentages
          </p>
          <h2 class="text-2xl font-semibold tracking-tight sm:text-3xl">
            Current roadmap
          </h2>
          <p class="max-w-3xl text-sm leading-6 text-slate-400">
            Implemented systems, active development tracks, committed next steps, and open research are separated so progress is not
            overstated. Open a group for its full list.
          </p>
        </div>

        <div class="grid gap-3 lg:grid-cols-2">
          <details
            v-for="group in roadmapGroups"
            :key="group.id"
            class="group rounded-2xl border border-slate-800 bg-slate-900/60 p-5 open:border-cyan-400/30"
          >
            <summary class="cursor-pointer list-none focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-cyan-300">
              <span class="flex items-center justify-between gap-4">
                <span
                  class="rounded-full border px-2.5 py-1 text-xs font-semibold"
                  :class="statusClasses[group.status]"
                >
                  {{ group.title }}
                </span>
                <span
                  class="transition-transform group-open:rotate-180"
                  aria-hidden="true"
                >⌄</span>
              </span>
              <span class="mt-3 block text-sm leading-6 text-slate-400">{{ group.summary }}</span>
            </summary>
            <ul class="mt-4 flex flex-col gap-3 border-t border-slate-800 pt-4">
              <li
                v-for="item in group.items"
                :key="item.title"
                class="flex gap-3 text-sm leading-6 text-slate-300"
              >
                <span
                  class="mt-2 size-1.5 shrink-0 rounded-full bg-cyan-300"
                  aria-hidden="true"
                />
                <span>
                  {{ item.title }}
                  <a
                    v-if="item.reference"
                    class="ml-1 text-xs text-cyan-300 underline-offset-4 hover:underline"
                    :href="item.reference.url"
                    rel="noreferrer"
                    target="_blank"
                  >
                    {{ item.reference.label }} ↗
                  </a>
                </span>
              </li>
            </ul>
          </details>
        </div>
      </section>

      <footer class="grid gap-4 border-t border-slate-800 pt-6 text-sm text-slate-500 sm:grid-cols-[minmax(0,1fr)_auto] sm:items-center">
        <p>
          {{ projectSummary.name }} is a source-built research renderer for NVIDIA Ada and Blackwell only.
        </p>
        <nav
          class="flex flex-wrap gap-4"
          aria-label="Repository links"
        >
          <a
            class="transition hover:text-cyan-300"
            :href="projectLinks.repository"
            rel="noreferrer"
            target="_blank"
          >
            GitHub ↗
          </a>
          <a
            class="transition hover:text-cyan-300"
            :href="projectLinks.license"
            rel="noreferrer"
            target="_blank"
          >
            License ↗
          </a>
          <a
            class="transition hover:text-cyan-300"
            href="#content"
          >
            Back to top ↑
          </a>
        </nav>
      </footer>
    </div>
  </main>
</template>
