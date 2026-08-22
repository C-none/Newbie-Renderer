export const projectLinks = {
  repository: 'https://github.com/C-none/Newbie-Renderer',
  license: 'https://github.com/C-none/Newbie-Renderer/blob/master/LICENSE',
};

export const projectSummary = {
  name: 'Newbie Renderer',
  eyebrow: 'Ada + Blackwell research renderer',
  tagline: 'Path tracing, modern GPU scheduling, and neural appearance experiments on Vulkan.',
  description: 'Newbie Renderer is a source-built research platform for Windows and NVIDIA Ada or Blackwell GPUs. '
    + 'Its default viewer combines a scene-driven Vulkan path tracer, DLSS Ray Reconstruction, a multi-queue render graph, '
    + 'and a Slang-based shader pipeline; neural appearance training remains an explicit experimental track.',
  support: 'Only NVIDIA Ada and Blackwell GPUs are supported. Other GPU architectures and operating systems are out of scope.',
};

export const highlights = [
  { label: 'Supported GPUs', value: 'NVIDIA Ada + Blackwell only' },
  { label: 'Runtime', value: 'Windows · Vulkan 1.4' },
  { label: 'Default pipeline', value: 'Path tracing + DLSS RR' },
  { label: 'Project status', value: 'Research · active development' },
];

export const caseStudies = [
  {
    status: 'implemented',
    kicker: 'Default viewer pipeline',
    title: 'Scene-driven path tracing with DLSS Ray Reconstruction',
    summary: 'The rtobject pipeline builds scene acceleration structures, prepares lights, traces one noisy sample per pixel, '
      + 'and sends color plus six explicit guides to the selected compute post-process before presentation.',
    details: [
      'The graphics batch orders acceleration-structure build, light preparation, Path Tracing, and UI. The compute tail '
        + 'runs either temporal accumulation or DLSS Ray Reconstruction, then converts and copies the result for presentation.',
      'DLSS Ray Reconstruction is selected by default. The normal interactive path uses DLAA; the command-line '
        + 'Ultra Performance selection exists only for the dedicated benchmark workflow.',
      'The path tracer publishes color, hardware depth, diffuse albedo, specular albedo, world normal plus roughness, motion, '
        + 'and specular hit distance as a fixed seven-image contract.',
    ],
    evidence: [
      {
        label: 'rtobject graph implementation',
        url: 'https://github.com/C-none/Newbie-Renderer/blob/master/src/pipeline/nrRtObjectPipeline.cpp',
      },
      {
        label: 'PathTracing and DLSS RR pass contract',
        url: 'https://github.com/C-none/Newbie-Renderer/blob/master/src/renderPasses/README.md',
      },
    ],
  },
  {
    status: 'experimental',
    kicker: 'Native GPU research track',
    title: 'Neural Appearance V3 training and comparison viewer',
    summary: 'A native Vulkan/Slang trainer publishes a validated, scene-independent neural-appearance artifact and compares '
      + 'native shading, neural inference, and absolute error in a dedicated viewer.',
    details: [
      'Training keeps FP32 master parameters and Adam state while producing a canonical FP16 row-major deployment model.',
      'The stable graph evaluates targets, clears and evaluates cooperative-vector gradients, converts them to canonical '
        + 'layout, optimizes FP32 state, quantizes FP16 weights, and evaluates held-out quality before publication.',
      'The comparison viewer presents native, neural, and amplified absolute-error columns for diffuse and specular lobes.',
    ],
    development: [
      {
        label: 'Implemented now',
        summary: 'GPU training, validation, FP16 artifact publication, and the native/neural/error comparison viewer.',
      },
      {
        label: 'In development',
        summary: 'A runtime material consumer that connects the published artifact to the main rtobject path tracer.',
      },
    ],
    evidence: [
      {
        label: 'GPU training design and measurements',
        url: 'https://github.com/C-none/Newbie-Renderer/blob/master/docs/neural_appearance_gpu_training.md',
      },
      {
        label: 'NeuralAppearance render-pass implementation',
        url: 'https://github.com/C-none/Newbie-Renderer/tree/master/src/renderPasses/NeuralAppearance',
      },
    ],
  },
  {
    status: 'implemented',
    kicker: 'Measured scheduling evidence',
    title: 'Cross-frame graphics and compute overlap',
    summary: 'Per-queue timelines and retained resource state allow current graphics work to overlap the longer reconstruction '
      + 'and presentation tail of older frames without introducing a global frame barrier.',
    details: [
      'Three frames in flight recycle only the current frame slot, leaving independent graphics, compute, and transfer work '
        + 'from other slots resident when dependencies permit.',
      'The captured interval shows Path Tracing on the graphics queue overlapping DLSS Ray Reconstruction on the compute queue.',
    ],
    evidence: [
      {
        label: 'Render graph executor architecture',
        url: 'https://github.com/C-none/Newbie-Renderer/blob/master/docs/architecture/README.md',
      },
    ],
    visualization: 'async-profile',
  },
];

export const features = [
  {
    category: 'Rendering results',
    status: 'implemented',
    title: 'Scene-Driven Path Tracing',
    summary: 'The default rtobject graph builds BLAS/TLAS data from the active scene and traces a fixed 1 spp noisy result '
      + 'with explicit material, light, and shader-binding-table metadata.',
    summaryEmphasis: ['rtobject', 'BLAS/TLAS', 'fixed 1 spp'],
    details: [
      'Scene extraction publishes cached geometry acceleration structures, per-frame instances, material sideband data, '
        + 'bindless textures, lights, and the hit-record plan consumed by the path tracer.',
      'Material and shadow rays use separate ray types. Alpha-mask and mixed single-sided geometry select explicit any-hit '
        + 'policies, while ordinary opaque geometry retains the hardware fast path.',
    ],
    evidence: [
      {
        label: 'rtobject pipeline',
        url: 'https://github.com/C-none/Newbie-Renderer/blob/master/src/pipeline/nrRtObjectPipeline.cpp',
      },
    ],
    tags: ['Path Tracing', 'BLAS', 'TLAS', 'SBT'],
  },
  {
    category: 'Rendering results',
    status: 'implemented',
    title: 'DLSS Ray Reconstruction',
    summary: 'Path Tracing publishes a seven-image Ray Reconstruction input contract. DLAA is the default interactive mode; '
      + 'Ultra Performance is available only through the dedicated benchmark command line.',
    summaryEmphasis: ['seven-image', 'DLAA', 'dedicated benchmark'],
    details: [
      'The integration owns quality-driven render resolution, history resets, camera jitter, exposure, and output-bypass '
        + 'rules through the renderer graph rather than hiding them in the application loop.',
      'The renderer keeps one persistent input set per frame in flight and transfers ownership from graphics to compute at '
        + 'the precise consumer stages derived by the graph.',
    ],
    evidence: [
      {
        label: 'DLSS RR contract tests',
        url: 'https://github.com/C-none/Newbie-Renderer/blob/master/test/unit/renderPasses/nr_renderpasses_dlss_rr_contract_test.cpp',
      },
    ],
    tags: ['DLSS RR', 'DLAA', 'Dynamic Resolution'],
  },
  {
    category: 'Rendering results',
    status: 'implemented',
    title: 'Scene Import and Material Coverage',
    summary: 'The viewer loads glTF and GLB scenes through the project load, resource, and scene layers, including the material '
      + 'and instancing cases covered by the smoke suite.',
    summaryEmphasis: ['glTF and GLB', 'smoke suite'],
    details: [
      'Covered cases include GLB input, KHR_texture_transform, KHR_materials_specular, MikkTSpace tangent fallback, '
        + 'multiple UV sets, and EXT_mesh_gpu_instancing.',
    ],
    evidence: [
      {
        label: 'Assimp load smoke coverage',
        url: 'https://github.com/C-none/Newbie-Renderer/blob/master/test/smoke/load/nr_load_assimp_smoke_test.cpp',
      },
    ],
    tags: ['glTF', 'GLB', 'Materials', 'Instancing'],
  },
  {
    category: 'Rendering results',
    status: 'implemented',
    title: 'UI, HDR, and Capture',
    summary: 'ImGui tooling integrates with SDR, HDR10, and scRGB presentation conversion, selectable tone mapping, screenshots, '
      + 'and GPU readback.',
    summaryEmphasis: ['SDR, HDR10, and scRGB'],
    details: [
      'The presentation path selects the supported swapchain format and color space, applies display conversion, and '
        + 'composites the UI before the final swapchain copy.',
      'The gallery below contains OpenEXR captures produced by the renderer and inspected client-side in the browser.',
    ],
    tags: ['ImGui', 'HDR10', 'scRGB', 'OpenEXR'],
  },
  {
    category: 'Renderer systems',
    status: 'implemented',
    title: 'C++26 Modules and Slang Reflection',
    summary: 'Project-owned host code uses C++26 modules, while reusable Slang modules and reflection keep resource layouts, '
      + 'descriptor bindings, and host/shader contracts synchronized.',
    summaryEmphasis: ['C++26 modules', 'Slang modules', 'reflection'],
    details: [
      'The project deliberately targets the LLVM/Clang toolchain required by its import std and module workflow.',
      'Third-party C/C++ dependencies are surfaced through narrow dependency modules instead of leaking raw headers through '
        + 'the renderer.',
    ],
    tags: ['C++26', 'Slang', 'Reflection'],
  },
  {
    category: 'Renderer systems',
    status: 'implemented',
    title: 'Link-Time Shader Variants and Persistent Caches',
    summary: 'Typed constants and interface-constrained concrete types select shader behavior at Slang link time; validated '
      + 'module, SPIR-V, and Vulkan pipeline-binary artifacts reuse work at distinct compilation stages.',
    summaryEmphasis: ['Typed constants', 'interface-constrained concrete types', 'Vulkan pipeline-binary'],
    details: [
      'A variant description accepts Boolean, integer, floating-point, and concrete-type assignments. ShaderService emits '
        + 'a synthetic Slang module and composes it with the root module and its sole entry point.',
      'Validated .slang-module blobs preserve shared front-end work. Slang entry-point hashes address persistent SPIR-V, '
        + 'while PipelineService fingerprints the complete PSO and stores the driver-defined VK_KHR_pipeline_binary payload.',
      'Linked-program objects are request-local and GPU assembly is not treated as a portable project artifact.',
    ],
    references: [
      {
        label: 'Slang link-time specialization',
        url: 'https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/10-link-time-specialization.html',
      },
      {
        label: 'VK_KHR_pipeline_binary',
        url: 'https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_pipeline_binary.html',
      },
    ],
    tags: ['Link-Time', 'Generics', 'SPIR-V Cache', 'Pipeline Binary'],
    visualization: 'variant-system',
  },
  {
    category: 'Renderer systems',
    status: 'implemented',
    title: 'Render Dependency Graph and Cross-Frame Queues',
    summary: 'Typed pass declarations let the compiler derive RAW/WAR/WAW barriers, layouts, queue ownership, and submission '
      + 'batches. Per-queue timelines preserve dependencies while allowing eligible graphics, compute, and transfer work '
      + 'from different frames to overlap.',
    summaryEmphasis: ['RAW/WAR/WAW barriers', 'Per-queue timelines', 'overlap'],
    details: [
      'The graph models buffers, images, late-bound swapchain images, acceleration structures, and CPU frame data. Matching '
        + 'compiled structures are reused while current-frame resources and callbacks are patched.',
      'Worker secondary command buffers record in parallel, and retained resources carry final queue, access, layout, and '
        + 'timeline state across frames.',
      'The rtobject graph records acceleration-structure build, light preparation, Path Tracing, and UI on graphics before '
        + 'DLSS Ray Reconstruction and presentation consume the result on compute.',
    ],
    evidence: [
      {
        label: 'Renderer architecture',
        url: 'https://github.com/C-none/Newbie-Renderer/blob/master/docs/architecture/README.md',
      },
    ],
    tags: ['RDG', 'Barriers', 'Async Compute', 'Async Copy'],
    visualization: 'cross-frame-async',
  },
  {
    category: 'Research experiments',
    status: 'experimental',
    title: 'Filter After Shading — First Stage',
    summary: 'An off-by-default A/B graph option selects an ABI-stable Slang root variant and performs one-sample stochastic '
      + 'bilinear reconstruction at LOD0 for the current material texture contract.',
    summaryEmphasis: ['off-by-default A/B', 'stochastic bilinear reconstruction'],
    details: [
      'The path tracer reserves a fixed random-dimension schedule across fourteen material texture semantics so optional '
        + 'layers and policy selection do not perturb unrelated sampling dimensions.',
      'Alpha-mask any-hit remains deterministic across the native and stochastic filtering policies.',
    ],
    development: [
      {
        label: 'Implemented now',
        summary: 'The off-by-default A/B path, fixed random dimensions, and one-sample LOD0 stochastic reconstruction.',
      },
      {
        label: 'In development',
        summary: 'Mip selection and texture-footprint support based on derivatives or ray cones.',
      },
    ],
    references: [
      {
        label: 'Filtering After Shading with Stochastic Texture Filtering',
        url: 'https://research.nvidia.com/labs/rtr/publication/pharr2024stochtex/',
      },
    ],
    tags: ['FAS', 'A/B', 'Stochastic Filtering'],
  },
  {
    category: 'Research experiments',
    status: 'experimental',
    title: 'GPU Neural Appearance V3',
    summary: 'A dedicated native GPU trainer and viewer publish a validated FP16 neural-appearance artifact and compare native, '
      + 'neural, and absolute-error results.',
    summaryEmphasis: ['native GPU trainer', 'validated FP16', 'absolute-error'],
    details: [
      'The deployment model is scene independent and represents resolved base-surface diffuse and specular reflection.',
    ],
    evidence: [
      {
        label: 'Neural Appearance V3 design and measurements',
        url: 'https://github.com/C-none/Newbie-Renderer/blob/master/docs/neural_appearance_gpu_training.md',
      },
    ],
    development: [
      {
        label: 'Implemented now',
        summary: 'GPU training, held-out validation, FP16 artifact publication, and the comparison viewer.',
      },
      {
        label: 'In development',
        summary: 'The rtobject runtime material consumer for the published neural-appearance artifact.',
      },
      {
        label: 'Research scope',
        summary: 'Transmission and layered-material support remain future research.',
      },
    ],
    tags: ['Neural Appearance', 'Cooperative Vector', 'FP16'],
  },
];

export const architectureLayers = [
  {
    title: 'Viewer and pipelines',
    summary: 'The application selects a registered graph, loads a model, owns camera and interaction state, and presents '
      + 'normalview or the default rtobject path.',
    details: [
      'normalview: world-space NormalBuffer → UI → Present.',
      'rtobject: AS Build → Light Prepare → Path Tracing → UI → Accumulate or DLSS RR → Present.',
    ],
  },
  {
    title: 'Load, resources, and scene',
    summary: 'CPU-side decoding stays separate from persistent resource values and the flecs-backed scene runtime.',
    details: [
      'The load layer decodes external assets into detached CPU values.',
      'The resource layer owns renderer-independent scene, material, texture, mesh, camera, and environment values.',
      'The scene layer instantiates ECS entities, uploads GPU scene data, and publishes renderer extraction snapshots.',
    ],
  },
  {
    title: 'Renderer graph and passes',
    summary: 'Pass builders declare typed resource usage; the compiler owns ordering, barriers, queue transitions, and submission.',
    details: [
      'Render passes own feature-specific preparation and recording but do not hand-author global synchronization.',
      'The executor compiles reusable structures, records worker command buffers, and carries retained state across frames.',
    ],
  },
  {
    title: 'RHI and Vulkan capability gate',
    summary: 'RAII-style Vulkan ownership, memory allocation, swapchain presentation, ray tracing, shader compilation, and '
      + 'pipeline binaries live behind a narrow RHI.',
    details: [
      'Startup requires a discrete NVIDIA device, Vulkan 1.4, ray tracing, pipeline binary, Maintenance8/9, cooperative '
        + 'vector training, float8, and the required queue topology.',
      'The renderer fails fast when a required capability is absent; this does not broaden support beyond Ada and Blackwell.',
    ],
  },
  {
    title: 'Shaders and research tracks',
    summary: 'Slang modules, reflection, variants, and persistent target artifacts serve both the production render path and '
      + 'isolated research experiments.',
    details: [
      'Entry-point ownership and link-time assignments keep reusable shader modules type checked.',
      'Neural Appearance V3 remains an explicit trainer/viewer track until a future rtobject consumer is implemented.',
    ],
  },
];

export const roadmapGroups = [
  {
    id: 'implemented',
    status: 'implemented',
    title: 'Implemented',
    summary: 'Available in the current codebase with code or test evidence.',
    items: [
      { title: 'Vulkan RHI with modular RAII-style resource ownership.' },
      { title: 'Slang compilation, reflection, link-time variants, persistent SPIR-V, and pipeline-binary caches.' },
      { title: 'flecs scene runtime and smoke-covered glTF/GLB ingestion.' },
      { title: 'Scene-driven BLAS/TLAS construction, RT metadata, SBT planning, and alias-table punctual-light sampling.' },
      { title: 'Cross-frame multi-queue RDG execution with Async Compute and Async Copy.' },
      { title: 'Path Tracing, temporal accumulation, DLSS Ray Reconstruction, HDR presentation, screenshots, and readback.' },
    ],
  },
  {
    id: 'experimental',
    status: 'experimental',
    title: 'Experimental',
    summary: 'Implemented research slices whose integration or scope is intentionally incomplete.',
    items: [
      {
        title: 'Filter After Shading first stage: fixed random dimensions and one-sample LOD0 stochastic reconstruction.',
        reference: {
          label: 'Filtering After Shading',
          url: 'https://research.nvidia.com/labs/rtr/publication/pharr2024stochtex/',
        },
      },
      {
        title: 'GPU Neural Appearance V3 training, validation, artifact publication, and native/neural/error comparison.',
      },
    ],
  },
  {
    id: 'next',
    status: 'next',
    title: 'Next',
    summary: 'Nearer-term integration work with a concrete renderer outcome.',
    items: [
      { title: 'Light BVH for scalable light selection.' },
      { title: 'A runtime consumer that integrates the V3 neural-appearance artifact into rtobject materials.' },
      { title: 'Publish reproducible Ada and Blackwell capture/benchmark metadata.' },
    ],
  },
  {
    id: 'research',
    status: 'research',
    title: 'Research directions',
    summary: 'Paper-driven directions, not delivery commitments.',
    items: [
      {
        title: 'Neural texture compression.',
        reference: {
          label: 'Random-Access Neural Compression of Material Textures',
          url: 'https://research.nvidia.com/labs/rtr/neural_texture_compression/',
        },
      },
      {
        title: 'Neural radiance caching.',
        reference: {
          label: 'Real-time Neural Radiance Caching for Path Tracing',
          url: 'https://research.nvidia.com/publication/2021-06_real-time-neural-radiance-caching-path-tracing',
        },
      },
      {
        title: 'ReSTIR PT / GRIS.',
        reference: {
          label: 'Generalized Resampled Importance Sampling',
          url: 'https://research.nvidia.com/publication/2022-07_generalized-resampled-importance-sampling-foundations-restir',
        },
      },
      {
        title: 'NeuSample and neural material importance sampling.',
        reference: {
          label: 'NeuSample',
          url: 'https://cseweb.ucsd.edu/~viscomp/projects/neusample/',
        },
      },
      { title: 'Neural shading optimization stability and broader layered-material models.' },
    ],
  },
];
