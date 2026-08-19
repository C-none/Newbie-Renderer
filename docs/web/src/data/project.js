export const projectLinks = {
  repository: 'https://github.com/C-none/Newbie-Renderer',
  readme: 'https://github.com/C-none/Newbie-Renderer#readme',
  architecture: 'https://github.com/C-none/Newbie-Renderer/blob/master/docs/architecture/README.md',
};

export const projectSummary = {
  name: 'Newbie Renderer',
  tagline: 'A research renderer built on C++26 modules, Slang, and Vulkan.',
  description: 'The target stack is intentionally narrow: Windows, Vulkan, and RTX-class NVIDIA hardware. '
    + 'The goal is a clean experimental platform for advanced rendering algorithms, modern resource management, '
    + 'and neural rendering workflows instead of broad compatibility.',
};

export const highlights = [
  { label: 'Language', value: 'C++26 modules' },
  { label: 'Shading', value: 'Slang + reflection' },
  { label: 'Backend', value: 'Vulkan 1.4 RHI' },
  { label: 'Upscaling', value: 'DLSS Ray Reconstruction' },
];

export const features = [
  {
    title: 'Modular Architecture',
    summary: 'Host-side code is organized with C++26 modules, while shader code is organized around Slang modules and reusable '
      + 'multi-entrypoint shader programs. This keeps compile boundaries explicit and makes the renderer easier to scale '
      + 'without turning the codebase into a header jungle.',
    tags: ['C++26', 'Slang'],
  },
  {
    title: 'Shader Reflection',
    summary: 'Slang reflection is already used to inspect shader parameters, validate resource layouts, build descriptor bindings, '
      + 'and keep host/shader contracts synchronized with less manual bookkeeping.',
    tags: ['Reflection', 'Descriptors'],
  },
  {
    title: 'Scene-Driven Ray Tracing',
    summary: 'Scene data drives cached BLAS rebuilds, per-frame TLAS construction, and the RT metadata and SBT plan consumed by ray-tracing passes.',
    tags: ['BLAS', 'TLAS', 'SBT'],
  },
  {
    title: 'Path Tracing + DLSS Ray Reconstruction',
    summary: 'Path tracing produces a fixed 1spp seven-resource RR input set. DLSS quality selects render resolution; '
      + 'DLAA renders at display resolution and is the only mode that permits output bypass.',
    tags: ['Path Tracing', 'DLSS'],
  },
  {
    title: 'Filter After Shading (FAS)',
    summary: 'RT path-traced materials expose an off-by-default A/B graph option backed by ABI-stable Slang root variants. '
      + 'The path tracer reserves a fixed three-rand4 schedule for eleven material texture semantics regardless of the selected '
      + 'policy or optional material layers; when enabled, FAS selects one bilinear reconstruction tap and performs one nearest '
      + 'LOD0 fetch per lookup. Changing the option resets temporal history, while alpha-mask any-hit remains deterministic. '
      + 'The implemented filtering path is intentionally mipless and does not use derivatives or ray cones.',
    tags: ['Stochastic Filtering', 'A/B'],
  },
  {
    title: 'Async Copy and Async Compute',
    summary: 'The multi-queue RDG implements asynchronous copy work on the transfer queue and asynchronous compute work on the '
      + 'compute queue, synchronized with graphics through timeline semaphores and queue-ownership transitions.',
    tags: ['RDG', 'Multi-Queue'],
  },
  {
    title: 'UI and HDR Presentation',
    summary: 'ImGui tooling integrates with SDR, HDR10, and scRGB presentation conversion, selectable tone mapping, screenshots, and readback.',
    tags: ['ImGui', 'HDR10', 'scRGB'],
  },
];

export const roadmap = [
  { done: true, title: 'RHI layer abstraction around Vulkan with modular RAII-style resource management.' },
  { done: true, title: 'Slang compilation and reflection pipeline for reusable shader/module workflows.' },
  { done: true, title: 'flecs integration as the scene-layer ECS runtime.' },
  { done: true, title: 'Asset import and decode foundation for glTF-oriented content ingestion.' },
  { done: true, title: 'Multi-queue RDG execution with Async Copy and Async Compute.' },
  { done: true, title: 'Scene-driven cached BLAS rebuilds with per-frame TLAS construction and RT metadata.' },
  {
    done: true,
    title: 'Alias-table many-light sampling for active punctual lights.',
    reference: {
      label: 'Dynamic Many-Light Sampling for Real-Time Ray Tracing',
      url: 'https://research.nvidia.com/sites/default/files/pubs/2019-07_Dynamic-Many-Light-Sampling//MPC19.pdf',
    },
  },
  { done: false, title: 'Light BVH.' },
  {
    done: false,
    title: 'Neural Material System.',
    reference: {
      label: 'Real-Time Neural Appearance Models',
      url: 'https://research.nvidia.com/labs/rtr/neural_appearance_models/',
    },
  },
  {
    done: false,
    title: 'NTC.',
    reference: {
      label: 'Random-Access Neural Compression of Material Textures',
      url: 'https://research.nvidia.com/labs/rtr/neural_texture_compression/',
    },
  },
  {
    done: false,
    title: 'Neural Radiance Caching.',
    reference: {
      label: 'Real-time Neural Radiance Caching for Path Tracing',
      url: 'https://research.nvidia.com/publication/2021-06_real-time-neural-radiance-caching-path-tracing',
    },
  },
  {
    done: true,
    title: 'RT Filter After Shading (FAS) first stage: an ABI-stable root A/B variant, fixed random-dimension mapping for eleven '
      + 'material texture semantics, and one-sample stochastic bilinear reconstruction at LOD0.',
    reference: {
      label: 'Filtering After Shading with Stochastic Texture Filtering',
      url: 'https://research.nvidia.com/labs/rtr/publication/pharr2024stochtex/',
    },
  },
  {
    done: true,
    title: 'DLSS Ray Reconstruction with quality-driven render resolution and DLAA-only output bypass.',
    reference: {
      label: 'DLSS Developer Resources',
      url: 'https://developer.nvidia.com/dlss',
    },
  },
  {
    done: false,
    title: 'ReSTIR PT / GRIS.',
    reference: {
      label: 'Generalized Resampled Importance Sampling: Foundations of ReSTIR',
      url: 'https://research.nvidia.com/publication/2022-07_generalized-resampled-importance-sampling-foundations-restir',
    },
  },
  {
    done: false,
    title: 'NeuSample / neural material importance sampling.',
    reference: {
      label: 'NeuSample: Importance Sampling for Neural Materials',
      url: 'https://cseweb.ucsd.edu/~viscomp/projects/neusample/',
    },
  },
  {
    done: false,
    title: 'Neural shading optimization stability.',
    reference: {
      label: 'Taming Optimization Variance in Compact Neural Shading Networks',
      url: 'https://research.nvidia.com/labs/rtr/publication/bitterli2026taming/',
    },
  },
  {
    done: false,
    title: 'Comprehensive neural materials.',
    reference: {
      label: 'Towards Comprehensive Neural Materials: Dynamic Structure-Preserving Synthesis with Accurate Silhouette at Instant Inference Speed',
      url: 'https://dl.acm.org/doi/full/10.1145/3721238.3730626',
    },
  },
];
