# Agent Interaction and Offline Automation Design

Status: implemented V1 contract; rotating NDJSON observability added on 2026-07-28.

Decision date: 2026-07-24.

Last revised: 2026-07-30.

This document is the source of truth for the implemented human, WebSocket-agent, and
offline-Lua interaction architecture in Newbie-Renderer. It replaces the earlier design
based on separate property/action concepts, multi-value mutation, retained task records,
and operation-result polling.

The words **must**, **must not**, **should**, and **may** are normative in this document:

- **must / must not** define the required V1 contract;
- **should** defines the recommended implementation unless concrete source constraints
  prove that a different implementation is safer;
- **may** describes an optional extension that must preserve the required contract.

This is both the V1 architecture contract and its implementation map. Sections marked
historical describe the pre-migration source only; all other implementation claims refer
to the current tree.

## 1. Final Decisions

The following decisions are fixed for this V1 contract.

| Concern | V1 decision |
|---|---|
| Semantic surface | Dear ImGui, WebSocket, and Lua use one option catalog, one immutable snapshot model, and the same single-option mutation entry |
| Public concepts | There is no property/action distinction |
| Mutation shape | One request targets exactly one semantic option; there is no `set_many`, JSON Patch, batch mutation, or multi-target transaction |
| Frame limit | Across the entire `OptionSystem`, at most one new explicit mutation is executed at the start of one renderable frame |
| Reads | Reads do not consume the mutation slot; transport rate and size limits still apply |
| Camera | Camera pose, WASD/QE movement, and mouse-look are mutations and consume the same global slot |
| Human camera input | All movement axes and mouse delta sampled for one frame are combined into one atomic camera-pose mutation |
| Effect time | A mutation submitted after snapshot N is frozen can only affect a later snapshot |
| Admission response | A mutation returns only `started` or `rejected`; `started` means that `OptionSystem` atomically reserved the single slot |
| Domain result | Final execution success or failure is never returned through the mutation response; it is written as `NR_OPTION_V1` to the rotating `build/app/logs/options.ndjson` file |
| Explicit rejection | If the caller receives `rejected`, the operation was not reserved and must never execute later |
| Lost response | A disconnected or timed-out client has an unknown outcome; the system does not claim that the operation failed |
| Delivery model | Client and server make an at-most-once attempt; neither side automatically retries |
| Duplicate suppression | There is no operation-ID ledger, durable receipt cache, or deduplication store |
| Long work | Only the initial start consumes a frame slot; later CPU/GPU/I/O continuation does not consume another slot |
| Retained tasks | There is no TaskStore, task ID, progress API, result polling, TTL, cancellation API, or task notification |
| Stale binding protection | Every mutation carries either the current `binding_epoch` or the corresponding opaque `snapshot_token`; reads may omit both |
| Derived model update | Successful `viewer.model.source` replacement also resets pose, FOV, and clip planes from the new scene/default camera in the same commit and the same terminal record; it is not a second mutation |
| Graph semantics | Actionable PathTracing, Accumulate, DLSS, and Present semantic nodes are singletons; graph preflight rejects duplicates |
| Fullscreen | `viewer.window.fullscreen` is a session option and has no direct `UiNode`/presentation mutation bypass |
| Capture | Renderer capture stays EXR-only; the protocol returns no pixels, image bytes, conversion, or path result |
| Capture completion | Capture is harvested only inside the Renderer after `Device::beginFrame()` has reclaimed the owning frame slot; minimized presentation delays harvest until rendering resumes, graph flush, or shutdown |
| Visual inspection | An agent that needs to see the application must use its own environment screenshot capability |
| Lua exposure | Lua receives a true per-function positive allowlist, not a blacklist applied after opening all libraries |
| Lua trust boundary | Automation-root scripts are local trusted scripts; budgets limit mistakes and resource use but do not claim isolation from malicious CPU consumption |
| Lua determinism | Frame resume/admission order is deterministic; `pairs`, `next`, and math results are not promised bitwise-identical across runs or platforms |
| MCP | The sidecar is deferred and is not implemented in this migration; any future mutating tool must use ordinary results and declare `taskSupport: "forbidden"` |
| Error facility | All implementation diagnostics use `nr.utils:errorHandle`; this design adds no second logging system |

These decisions intentionally trade throughput and result convenience for a small,
deterministic state machine.

## 2. Problem Statement

Before this implementation, the renderer exposed settings through app-owned Dear ImGui
code and node-owned UI callbacks. Some controls staged values in node-local draft/pending
fields, pipeline controls staged independent requests, and camera input mutated the viewer
camera directly. That arrangement was usable by a human, but could not safely become an
agent or scripting API:

- display labels are not stable machine identifiers;
- callback lifetime is tied to a particular UI frame and often captures mutable node
  state;
- a network thread cannot call ImGui or renderer callbacks;
- independently rebuilding the same settings for WebSocket and Lua would create three
  sources of truth;
- different pending fields can currently permit more than one change during one frame;
- direct camera input bypasses any general mutation arbitration;
- node-local UI drafts can diverge from the values observed by the renderer.

The target design therefore moves ownership of adjustable semantic state into one
independent `OptionSystem`. Nodes register their semantics with that system, but they do
not retain a second user-editable copy. Every renderable frame receives one immutable
option snapshot. The UI, renderer, WebSocket readers, and Lua readers all observe that
same published state.

## 3. Goals

- Replace parallel UI/agent/script control implementations with one semantic option
  catalog.
- Keep human Dear ImGui controls and WASD/QE plus mouse-look behavior.
- Give a live local agent a bounded JSON-RPC 2.0 WebSocket interface.
- Give offline automation a deterministic embedded Lua interface.
- Make option registration, validation, admission, frame timing, and stale-binding checks
  identical for all mutation producers.
- Ensure renderer and UI code use one immutable snapshot for the whole frame.
- Execute renderer-domain mutation only on the application/render thread.
- Make one-frame mutation ordering explicit and testable.
- Preserve the domain state that is genuinely required for GPU work, capture readback,
  graph lifetime, and resource loading without turning it into a public task system.
- Keep networking, JSON, and Lua dependencies behind narrow `dependency.*` modules.
- Keep the option core independent of Dear ImGui, WebSocket, Lua, Vulkan, and render-pass
  implementation types.

## 4. Non-Goals

- Pixel-coordinate automation of Dear ImGui.
- Synthetic mouse/keyboard control by an agent.
- Returning the final result of a started mutation through WebSocket or Lua.
- Exactly-once delivery across a broken network connection.
- Automatic retry, request replay, durable request identity, or deduplication.
- Mutation queues deeper than one entry.
- Multiple mutations in one request or one render frame.
- Atomic transactions across unrelated options.
- Task creation, progress reporting, result polling, cancellation, or task retention.
- Event subscriptions or a general semantic event bus.
- Exposing raw Dear ImGui, renderer, render-graph, node, RHI, Vulkan, or filesystem objects
  to WebSocket or Lua.
- Streaming screenshots, converting EXR to another format, or returning capture bytes.
- Internet-facing service operation, multi-user authorization, or remote administration.
- Making the renderer headless.
- Adding a Linux RHI path. V1 runtime remains aligned with the project's Windows-only RHI
  scope, although the pure option model should remain platform-neutral.

## 5. Terminology

### 5.1 Option

An **option** is one stable semantic address with:

- a stable ID;
- one input schema;
- one current read representation;
- validation and availability rules;
- one frame-thread executor;
- optional UI presentation hints.

The protocol does not label an option as a property or an action. For example,
`viewer.camera.pose`, `render.present.ui_opacity`, and
`render.present.capture_exr` are all addressed through the same operation:

```text
option.apply(id, value, binding)
```

Their domain behavior differs, but their public mutation mechanism does not.

An option whose input is a one-frame trigger still has a read representation. In V1 that
representation is the same closed empty object `{}` plus current `available` and
`unavailable_reason` fields. It does not expose a last-result, progress, or retained
operation record.

### 5.2 Mutation

A **mutation** is one attempt to apply one complete value to one option ID. It is never a
partial object patch and never contains multiple option IDs.

A structured value is permitted only when the domain value is semantically indivisible.
Examples are a camera pose and a near/far clip-plane pair. A structured value must not be
introduced merely to hide an unrelated multi-option transaction.

### 5.3 Admission

**Admission** is the atomic act of reserving the single pending slot. Admission performs
transport-independent checks that are safe outside the renderer:

- authority;
- server lifecycle;
- binding freshness;
- option existence;
- input type and range;
- currently published availability;
- admission-gate state;
- pending-slot vacancy.

Admission does not call renderer or node code.

### 5.4 Started

`started` means only:

> The mutation passed admission and `OptionSystem` atomically stored it in the single
> pending slot.

It does not mean that:

- the next frame already began;
- the domain executor succeeded;
- a graph was installed;
- a model or environment loaded;
- a GPU copy completed;
- an EXR file was written.

### 5.5 Rejected

`rejected` means:

> No pending mutation was created by that request, and that request will never execute
> later.

An explicit rejection is the only negative transport result. A missing response is not an
explicit rejection.

### 5.6 Renderable frame

A **renderable frame** is an application iteration in which the framebuffer is available
and the renderer will attempt to consume a newly frozen frame snapshot. A minimized or
zero-extent presentation iteration is not a renderable frame for this contract.

### 5.7 Continuation

A **continuation** is required domain work after the initial mutation has started, such as:

- capture readback completion;
- EXR file writing;
- an already-started GPU/resource transition during graph flush.

A continuation is not a new mutation and does not consume another global frame slot. It is
also not a public task.

### 5.8 Binding epoch and snapshot token

`binding_epoch` identifies the currently installed set of option IDs, schemas, scopes,
lifetimes, and graph generation. It changes when that catalog binding changes, not when an
ordinary value changes.

`snapshot_token` is an opaque, session-scoped representation of the same binding view. It
is carried in every published snapshot so a client does not need to interpret numeric
epochs. It is intentionally stable across ordinary frame/value revisions within one
binding epoch.

The token is a stale-reference guard, not an authorization secret.

## 6. Pre-migration Source Baseline (Historical)

The table below records the mutation paths that existed before this design was
implemented. The migration replaced those paths in place instead of adding a second
registry beside them.

| Source | Pre-migration responsibility | Applied change |
|---|---|---|
| [`src/app/nrAppUi.ixx`](../src/app/nrAppUi.ixx) | Owns Dear ImGui and implements typed immediate controls | Becomes a presenter of `OptionFrameSnapshot`; it no longer owns canonical adjustable values |
| [`src/renderer/nrRenderer.ixx`](../src/renderer/nrRenderer.ixx) | Defines `NodeUiWriter`, `NodeUiBuildContext`, `NodeFrameParameters`, and `RendererFrameInput` | Replace mutable node-UI semantics with option registration and add an explicit const snapshot to frame input |
| [`src/renderer/nrRenderer.cpp`](../src/renderer/nrRenderer.cpp) | Calls `NodeRuntime::collectUi(...)` during frame construction | Stop using frame-local UI callbacks as semantic registration; nodes register at graph installation |
| [`src/pipeline/nrPipeline.cpp`](../src/pipeline/nrPipeline.cpp) | Owns `ViewerControlState`, four independent pending requests, UI controls, and the main frame loop | Move adjustable state and requests into `OptionSystem`; replace multi-request processing with one frame-boundary executor |
| [`src/app/nrAppCamera.cpp`](../src/app/nrAppCamera.cpp) | Samples input and directly calls `ViewerPerspectiveCamera::applyControl(...)` | Keep sampling/math/cursor tracking, but submit one camera-pose option instead of mutating the viewer directly |
| [`src/renderer/nrViewerCamera.ixx`](../src/renderer/nrViewerCamera.ixx) | Stores pose/lens/control configuration and builds frame camera data | Remains the camera-domain math/runtime type; canonical adjustable pose/lens values come from the option snapshot |
| [`src/renderPasses/Accumulate/nrAccumulateNode.ixx`](../src/renderPasses/Accumulate/nrAccumulateNode.ixx) | Stores UI draft and pending history limit | Remove user-editable draft/pending ownership; read the frame snapshot |
| [`src/renderPasses/PathTracing/nrPathTracingNode.ixx`](../src/renderPasses/PathTracing/nrPathTracingNode.ixx) | Stores a path-tracing variant UI draft and pending variant | Split independently adjustable values into individual options and derive the variant from the snapshot |
| [`src/renderPasses/DlssRayReconstruction/nrDlssRayReconstructionNode.ixx`](../src/renderPasses/DlssRayReconstruction/nrDlssRayReconstructionNode.ixx) | Stores `uiDraft_`, `pendingInput_`, and one-shot reset state | Move adjustable values to `OptionSystem`; retain only DLSS runtime and the minimal consumed-reset marker |
| [`src/renderPasses/Present/nrPresentNode.ixx`](../src/renderPasses/Present/nrPresentNode.ixx) | Stores UI drafts, pending tone/opacity, screenshot request counts, and capture runtime | Move tone/opacity and the new capture start to options; retain only real GPU/readback/EXR in-flight state |
| [`src/utils/errorHandle.cpp`](../src/utils/errorHandle.cpp) | Routes and flushes project logs | Remains the sole log facility, carries the stable option-operation record, and owns the rotating NDJSON sink |

The most important migration rule was:

> Do not connect WebSocket or Lua to `NodeUiSectionDrawCallback`, and do not keep
> `NodeUiWriter` as a hidden second semantic mutation path.

Those callbacks captured node-owned state and were safe only in their UI-frame lifetime.
Reusing them from another thread would have preserved the original ownership problem.

## 7. System Architecture

### 7.1 High-level data flow

```text
                         read immutable published snapshot
                  +---------------------------------------------+
                  |                                             |
        +---------v----------+  same read/apply API   +----------v---------+
        | Dear ImGui         |----------------------->|                    |
        | + human input      |                        |                    |
        +--------------------+                        |                    |
                                                     |                    |
        +--------------------+  same read/apply API   |    OptionSystem    |
        | WebSocket /        |----------------------->|                    |
        | JSON-RPC adapter   |                        |                    |
        +--------------------+                        |                    |
                                                     |                    |
        +--------------------+  same read/apply API   |                    |
        | embedded Lua       |----------------------->|                    |
        | adapter            |                        +---------+----------+
        +--------------------+                                  |
                                                               | one pending mutation
                                                               | + immutable active state
                                                    main/render-thread boundary
                                                               |
                                                     +---------v----------+
                                                     | Frame coordinator  |
                                                     | execute at most 1  |
                                                     | collect + freeze   |
                                                     +---------+----------+
                                                               |
                          +------------------------------------+----------------------+
                          |                                    |                      |
                  +-------v--------+                    +------v-------+       +------v-------+
                  | app/pipeline   |                    | renderer and |       | domain       |
                  | domain binding |                    | render nodes |       | continuations|
                  +----------------+                    +--------------+       +--------------+
                                                               |
                                                     nr.utils:errorHandle
                                                               |
                                            rotating engine/options NDJSON files
                                            + warning/error console mirror
```

The upper three producers never receive a mutable renderer object. The lower domain
executors never parse JSON, access a WebSocket connection, or access a Lua stack.

### 7.2 Module and target boundaries

The implementation adds an independent `nroptions` CMake target after `nrutils` and before
`nrrenderer`. It exports the top-level `nr.options` module as the
`nr.options:model`, `nr.options:system`, and `nr.options:registration` partitions. The
target depends only on the C++ standard library and `nr.utils`; it does not depend on app,
renderer, Dear ImGui, Vulkan, WebSocket, or Lua. A top-level module is required because
renderer and render-pass code consume the snapshot without importing the app layer.

| Module or component | Owns | Must not depend on |
|---|---|---|
| `nr.options:model` | `OptionId`, closed `OptionWireValue`, `OptionKey<T>`, `OptionSchema`, `OptionDefinition`, mutation/admission records, immutable snapshot and binding-token types | ImGui, WebSocket, Lua, renderer, Vulkan |
| `nr.options:system` | catalog, active values, published snapshot, single pending slot, admission gate, epoch/generation counters | ImGui, WebSocket, Lua, Vulkan, concrete nodes |
| `nr.options:registration` | `OptionCatalogBuilder`, graph/app definition construction, schema/default validation, and catalog size preflight | transport adapters |
| `nr.pipeline` | frame-boundary orchestration and calls into domain executors | raw third-party networking/Lua APIs |
| `nr.app:optionUi` | Dear ImGui presentation and human arbitration | WebSocket and Lua |
| `nr.interaction` | authenticated JSON-RPC transport adapter | renderer/node mutation APIs |
| `nr.automation` | allowlisted Lua host and frame coroutine scheduler | raw renderer/node objects |
| `nr.renderer` | explicit const snapshot input and frame consumers | app, WebSocket, Lua |
| `nr.renderPasses` | graph-scoped option declarations, domain execution, GPU runtime, continuation state | WebSocket, Lua |
| `dependency.json` | narrow, bounded Boost.JSON parsing and serialization adapter used by all project C++ JSON paths | project semantic policy |
| `dependency.network` | narrow Boost.Asio and Boost.Beast exposure | project semantic policy |
| `dependency.lua` | narrow Lua C API exposure | project semantic policy |

`OptionWireValue` is a closed representation containing booleans, bounded integers,
finite floating-point values, UTF-8 strings, arrays, and closed objects. It is not an
open-ended `std::any` or third-party JSON value. C++ domain consumers use `OptionKey<T>`;
nodes must not look up values by string.

### 7.3 Natural owner

[`AppSession`](../src/app/nrAppSession.ixx) is the natural runtime owner of `OptionSystem`
because it already owns the renderer, camera, UI, and scene session.

The lifecycle must guarantee:

1. `OptionSystem` exists before any app or graph option is registered.
2. WebSocket and Lua stop admitting work before graph teardown begins.
3. Renderer-owned continuations are flushed before their nodes are destroyed.
4. The catalog is data-only and retains no node pointer or callback across teardown.
5. The final snapshot is no longer used before `OptionSystem` is destroyed.

Member declaration order alone is not sufficient; shutdown explicitly closes the
admission gate before renderer graph teardown.

## 8. Hard Boundaries

### 8.1 Ownership boundary

`OptionSystem` owns:

- session definitions plus active-graph definitions;
- canonical active values;
- the atomically published immutable snapshot;
- at most one pending mutation;
- the admission gate and authority mode;
- binding epoch/token and graph generation;
- monotonic option-frame, snapshot-revision, and machine-log sequences.

It must not store leased mutations, restore/retry state, FIFO entries, completed results,
events, tasks, or callbacks inside the pending record. Pending copies only typed input,
option ID, epoch/generation, origin, optional request ID, and sequence.

Nodes own:

- GPU resources;
- derived pipelines and caches;
- frame history resources;
- active GPU/readback/file-write state;
- the minimum marker needed to consume a one-frame signal once.

Nodes must not own:

- a user-editable draft that shadows an option;
- a second pending queue for a semantic option;
- WebSocket/Lua-visible result state;
- a retained semantic task record.

### 8.2 Thread boundary

The networking I/O thread may:

- authenticate and maintain a WebSocket connection;
- parse and serialize bounded JSON;
- atomically load the published immutable snapshot;
- call the thread-safe admission function;
- enqueue bounded outbound bytes.

It must not:

- call ImGui;
- call a node executor;
- install/uninstall a graph;
- load a model or environment;
- mutate camera/runtime/node state;
- wait for a renderer frame while holding a network lock.

Initial domain dispatch, live binding lookup, canonical option commit, and renderer-state
mutation happen on the application/render thread. A domain-owned worker, GPU submission,
or I/O continuation may progress already-started work elsewhere, but it may only publish a
bounded completion record. It must not mutate `OptionSystem`, a node, or a published
snapshot directly. The application/render thread harvests that record at a safe point.

Catalog replacement, the admission gate, and the pending slot share one mutex so
binding validation and slot reservation have no TOCTOU gap. The published
`shared_ptr<const OptionFrameSnapshot>` is loaded and stored atomically. The WebSocket
thread may only load that pointer and call `trySchedule(...)`; executor, availability,
catalog replacement, and graph lifecycle operations stay on the application/render
thread. `FrameServices` never exposes `OptionSystem`.

### 8.3 Frame-consistency boundary

One `OptionFrameSnapshot` is frozen at the start of a renderable frame. The following
consumers use that exact snapshot:

- renderer frame preparation;
- structural render-graph decisions;
- node build/materialization;
- camera override construction;
- Dear ImGui option presentation;
- WebSocket and Lua reads after publication.

No consumer may fetch a newer mutable value during the same frame.

### 8.4 Graph-lifetime boundary

Every graph-scoped definition belongs to one `graph_generation` and one `binding_epoch`.
The data-only catalog has no runtime callback or node handle. Old immutable snapshots may
remain alive for readers, but their epoch/token cannot admit a new mutation after graph
replacement.

### 8.5 Protocol-result boundary

The synchronous protocol response covers admission only. The rotating
`build/app/logs/options.ndjson` file covers domain execution. No API may blur the
boundary by returning an “effective value,” “applied frame,” capture path, or final
success in a delayed response.

### 8.6 Image boundary

The renderer's capture boundary ends at writing its existing EXR file. WebSocket, Lua, and
the MCP adapter never:

- return the EXR bytes;
- convert EXR to PNG/JPEG;
- expose a framebuffer handle;
- take an operating-system screenshot.

Visual inspection is the external agent environment's responsibility.

### 8.7 Observability boundary

The renderer does not retain operation results for later query. It emits an
`NR_OPTION_V1` record and forgets the semantic operation after its domain work no longer
needs state. A launcher, agent, or human that needs current-session history reads
`build/app/logs/options.ndjson` plus its `.1` through `.4` rotated segments. A live reader
must reopen and scan the replacement active file from its `NR_LOG_SESSION_V1` marker
after rotation.

## 9. Unified Option Model

### 9.1 Conceptual definition

The public core model is fixed:

| Type | Contract |
|---|---|
| `OptionId` | stable lower-case ASCII dotted ID |
| `OptionWireValue` | closed bool/integer/number/UTF-8 string/array/closed-object value |
| `OptionKey<T>` | strongly typed key used by C++ domain code |
| `OptionSchema` | type, range, enum, object-field, size, and cross-field validation |
| `OptionDefinition` | ID, schema, default, scope, UI presentation metadata, and data-only post-commit temporal-reset policy |
| `OptionMutationRequest` | ID, complete value, binding proof, origin, and optional request ID |
| `ScheduleResult` | `started`, or `rejected` with one stable reason |
| `OptionFrameSnapshot` | immutable catalog/value/availability, monotonic frame/revision, graph/binding identity, and at most one frame effect |
| `OptionCatalogBuilder` | builds and validates one complete candidate catalog |
| `OptionSystem` | sole canonical value owner and sole pending-slot owner |

The conceptual `OptionDefinition` contains:

```text
id
title
description
input_schema
read_schema
constraints
presentation_hint
scope
schema_fingerprint
resets_temporal_history
```

It intentionally does not contain a public `kind = property|action` field.

`presentation_hint` is advisory metadata such as checkbox, combo, slider, text input, or
button. It controls only Dear ImGui rendering. It does not define semantics and is not
used by WebSocket or Lua to choose a different mutation path.

`resets_temporal_history` is internal definition metadata for retained values whose
successful transition invalidates temporal consumers. Pipeline execution reads it before
commit and requests the renderer reset only after canonical commit succeeds. It is not a
frame effect, does not claim a GPU batch, and does not reset the sampling ordinal.

### 9.2 Stable IDs

IDs must:

- use lower-case dot-separated ASCII segments;
- remain stable across label changes;
- describe semantics, not a widget;
- be unique within one binding epoch;
- not contain node addresses, array indices that change across runs, or localized text.

Examples:

```text
viewer.pipeline.selected
viewer.model.source
viewer.environment.source
viewer.rt.post_processing_mode
viewer.window.fullscreen
viewer.camera.pose
viewer.camera.vertical_fov_degrees
viewer.camera.clip_planes
viewer.camera.movement_speed
render.path_tracing.max_surface_bounces
render.path_tracing.russian_roulette_enabled
render.path_tracing.filter_after_shading_enabled
render.accumulate.max_history_samples
render.dlss.enabled
render.dlss.quality
render.dlss.bypass
render.dlss.visualize_motion_vectors
render.dlss.reset_history
render.present.tone_mapping
render.present.ui_opacity
render.present.capture_exr
```

These are the current V1 IDs. `viewer.rt.dlss_quality` is deliberately absent:
`--dlss-quality` seeds `render.dlss.quality` and does not create a second option. DLSS
preset selection is also absent from the option protocol and remains a programmatic
`DlssRayReconstructionNodeInput` setting.

### 9.3 One option per mutation

A mutation contains:

```text
origin
correlation
option_id
complete_input_value
binding_epoch or snapshot_token
admission_sequence
submitted_after_frame
graph_generation
```

Only `option_id`, input, and binding proof cross the public API. Origin, correlation,
sequence, and submitted-frame metadata are assigned or normalized internally for logs.

The mutation must not contain:

- a list of IDs;
- a field path into an option;
- a merge-patch document;
- an operation list;
- a requested retry count;
- an operation/task ID supplied by the caller.

One targeted option may naturally cause derived runtime state, availability, or the
graph-scoped catalog to change. Those consequences do not turn the request into
`set_many`; the request still supplies exactly one semantic target and one complete input.

Two cross-value commit cases are explicitly permitted:

- successful `viewer.model.source` replacement commits that source and the new
  scene/default camera pose, FOV, and clip planes atomically under the original mutation
  sequence;
- successful graph replacement installs the new graph-scoped defaults as part of binding
  replacement.

No other mutation may silently repair or change another canonical option. In particular,
`render.dlss.bypass=true` makes a later non-DLAA quality mutation invalid; callers must
first set bypass to false in an earlier frame.

### 9.4 Structured values

Structured values are full replacements.

`viewer.camera.pose` is one semantic value because position and orientation must describe
one coherent camera pose:

```json
{
  "position": [0.0, 1.5, 3.0],
  "yaw_degrees": -90.0,
  "pitch_degrees": 0.0
}
```

`viewer.camera.clip_planes` is one semantic value because `near < far` is a joint
invariant:

```json
{
  "near": 0.1,
  "far": 1000.0
}
```

These are not general-purpose batch containers. Pipeline selection and UI opacity, for
example, must not be grouped into one object.

### 9.5 Type safety

JSON is a transport representation, not the renderer's internal state type.

The recommended implementation uses typed registration with type erasure at the catalog
boundary:

1. a registration site declares a project-owned C++ value type;
2. it provides typed validation and serialization metadata;
3. the transport adapter converts bounded JSON into an intermediate project value;
4. admission converts and validates it against the immutable definition;
5. only the typed value is stored in the pending mutation;
6. render code never receives a `boost::json::value`.

This preserves modern C++ type safety while allowing one heterogeneous catalog.

### 9.6 Validation

Admission validation must reject:

- NaN and infinity;
- numbers outside declared ranges;
- unknown enum strings;
- missing or extra object fields where the schema is closed;
- invalid UTF-8;
- oversized strings, arrays, and objects;
- paths outside configured roots;
- malformed camera vectors or invalid clip planes;
- a value that cannot be represented by the target C++ type.

Validation shared by all producers lives with the option definition. UI widgets
may prevent common invalid input, but UI-only validation is not authoritative.

### 9.7 Availability

Every snapshot record contains:

```text
available: bool
unavailable_reason: optional stable code
```

Availability describes only shared domain capability at snapshot publication time and is
recomputed at frame collection. Examples:

- a DLSS option is unavailable when the active graph has no DLSS node;
- capture is unavailable while Present owns an in-flight capture;
- pipeline selection is unavailable while another domain transition makes replacement
  unsafe.

Admission rechecks availability. A stale snapshot that said `available=true` does not
force the server to accept a mutation.

Producer authority and the live admission gate are deliberately not encoded in per-option
availability:

- session/adapter state determines whether a producer is writable;
- the admission gate represents the current frame/graph/shutdown critical section;
- the shared option snapshot remains byte-for-byte semantically identical for Dear ImGui,
  WebSocket, and Lua.

A mutation may start only when producer writability, shared domain availability, the live
gate, and the global slot all permit it.

### 9.8 Scope and graph replacement

Options have an internal registration scope:

- **session-scoped:** camera, active pipeline, model, environment, and other app-level
  values;
- **graph-scoped:** values contributed by currently installed nodes.

Scope is lifetime metadata, not a public property/action category.

On graph replacement:

- session-scoped values remain;
- all old graph-scoped definitions and values are replaced;
- new graph-scoped definitions receive their declared initial values;
- V1 performs no implicit value migration merely because a new graph reuses an ID;
- an option that must persist across graphs should be intentionally promoted to session
  scope;
- the binding epoch changes before the new catalog is published.

This avoids accidental state carry-over between incompatible node implementations.

## 10. Registration and State Ownership

### 10.1 Registration timing

App-level options register during session initialization. Node options register once when
a graph is installed, not once per UI frame.

The required graph registration flow is:

1. create an `OptionCatalogBuilder` for the candidate graph generation;
2. retain the old graph and ask every candidate `NodeRuntime::declareOptions(...) const`
   implementation for pure definitions; declaration may not access `Device` or mutable
   runtime;
3. preflight node/submit indices, actionable semantic singletons, duplicate option IDs,
   schema/default validity, resolver-required keys, graph scope, and the serialized
   snapshot size bound;
4. only after the complete preflight succeeds, cross the destructive barrier: wait/flush
   old renderer continuations and tear down the old graph;
5. initialize the candidate runtime;
6. install the candidate nodes; domain code remains on the frame thread, and the Renderer
   collects conservative availability directly from those installed nodes;
7. atomically replace the complete graph catalog and graph-scoped values with the new
   defaults, plus incremented
   `graph_generation`, and incremented `binding_epoch`;
8. publish the complete snapshot and open admission.

An incomplete catalog must never be visible to readers.

Failure before step 4 leaves the old graph and catalog installed. Failure after the
destructive barrier follows the project fail-fast policy; V1 does not add a generic graph
rollback mechanism.

### 10.2 Replacing node UI semantics

The existing node semantic registration must be modified in place:

- `NodeRuntime::collectUi(...)` must stop being the source of adjustable semantics;
- node declarations move to `NodeRuntime::declareOptions(...) const`, a pure
  graph-install-time hook that cannot access `Device` or mutable runtime;
- graph/node availability uses a read-only collector, and renderer-owned long work
  advances through `advanceContinuations(...)`;
- `NodeUiWriter`'s mutable-reference controls are removed from the semantic path;
- `OptionUiPresenter` renders definitions and values from the frame snapshot;
- node UI drafts and option-specific pending fields are removed after each node migrates.

Pure non-actionable layout text may remain app presentation code, but it must not carry a
hidden mutation callback.

`collectUi()` and `NodeUiWriter` are ultimately removed from actionable semantics.
Non-control performance/status diagnostics may remain in a separate diagnostics section.

### 10.3 Data-only catalog and runtime routing

`OptionDefinition` contains schema, default, scope, lifetime, UI presentation metadata,
the data-only `resetsTemporalHistory` post-commit policy, and an admission validator. It
contains no node pointer, executor callback, availability callback, or transport object.

Runtime work follows explicit frame-thread paths:

- `nr.pipeline` dispatches session-domain mutations such as graph, model, environment,
  fullscreen, and canonical camera/value commits;
- ordinary graph values commit generically to `OptionSystem`, and installed nodes read
  those values from the mandatory frame snapshot;
- after a retained value commits successfully, `nr.pipeline` forwards its
  `resetsTemporalHistory` policy to the renderer; failed commits request no reset;
- the Renderer visits installed nodes to collect conservative availability;
- a frame effect is claimed through the frame-local `FrameEffectSink` and is finalized
  against the exact submitted batch;
- continuations advance through the installed node runtime hook.

These routes never run on the network thread, never retain a caller request, and never
send a protocol response. Because the catalog is data-only, the old immutable snapshot
can remain readable across graph teardown without retaining dead runtime callbacks.

### 10.4 Canonical versus derived state

Canonical adjustable state belongs to `OptionSystem`. Derived runtime state stays in the
domain.

Examples:

| State | Owner |
|---|---|
| camera pose, lens, and movement-speed options | `OptionSystem` |
| camera matrices and frustum | camera/renderer frame derivation |
| path-tracing bounce count option | `OptionSystem` |
| compiled path-tracing pipeline | PathTracing node |
| Present UI opacity option | `OptionSystem` |
| screenshot readback buffer and fence/frame slot | Present node |
| active pipeline ID | `OptionSystem` after successful install |
| installed graph objects | renderer |
| model source option | `OptionSystem` after successful load/commit |
| scene resources and upload state | scene/model controller |

## 11. Immutable Frame Snapshot

### 11.1 Conceptual snapshot

```text
OptionFrameSnapshot
  session_id
  frame_index
  snapshot_revision
  graph_generation
  binding_epoch
  snapshot_token
  optional frame_effect:
    sequence
    option_id
    typed_input
  options[]:
    definition
    current_value
    available
    unavailable_reason
```

Definitions may be structurally shared between snapshots so ordinary frames do not copy
the catalog. The externally serialized view must nevertheless appear self-consistent.

At most one `frame_effect` exists because a renderable frame detaches at most one explicit
mutation. Reset and capture use this frame-local carrier; it is not retained in later
snapshots and is not a public task/result record.

### 11.2 Token semantics

The token must include or be bound to:

- the current process/session identity;
- the current binding epoch.

It must not change merely because:

- the camera moved;
- a scalar option changed;
- a new frame was rendered;
- a continuation changed domain availability without rebinding.

If the token changed every frame, normal network latency would cause unnecessary stale
rejections. `snapshot_revision` and `frame_index` represent value-time changes; the token
represents binding identity.

If a mutation supplies both `binding_epoch` and `snapshot_token`, both must identify the
same current binding or admission rejects it.

### 11.3 Publication

Publication is one atomic pointer swap after frame collection completes. Readers either
see the complete previous snapshot or the complete new snapshot; they never see a
partially updated catalog/value set.

Session/initial graph installation publishes Snapshot 0 before external admission opens.
Subsequent renderable frames advance the independent monotonic option `frame_index`;
minimized iterations do not.

The network thread may retain a shared immutable snapshot while the render thread
publishes a newer one. This is memory-safe. It does not make the older binding valid for a
new mutation.

### 11.4 Explicit renderer input

The snapshot is a required, non-optional input in `RendererFrameInput`:

```text
RendererFrameInput
  ...
  options: const OptionFrameSnapshot&
```

It is forwarded as the same required, non-optional reference in `NodeFrameParameters`.
Every `renderFrame(...)` call site supplies it explicitly, and
`FrameResolutionResolver` receives the same snapshot used by camera derivation,
structural snapshotting, skeleton patching, materialization, and node build.

The snapshot should not be hidden in the existing mutable
[`FrameServices`](../src/renderer/nrFrameServices.ixx) type-index service map. Explicit
frame input makes the consistency boundary reviewable and prevents a node from finding a
newer mutable service during the frame.

### 11.5 Read semantics

Dear ImGui, WebSocket, and Lua read the published snapshot. Reads:

- do not occupy or inspect the mutation slot;
- do not wait for the next frame;
- may return the last published snapshot while minimized;
- never return speculative pending values;
- may show current availability, including domain busy state;
- do not expose retained operation results.

“Reads are unrestricted” means there is no semantic one-read-per-frame rule. It does not
disable connection rate limits, response-size limits, or outbound backpressure.

## 12. Single-Slot Mutation State Machine

### 12.1 Minimal state

The core needs only:

```text
definitions
active_values
published_snapshot
optional<pending_mutation>
admission_gate
binding_epoch
graph_generation
next_frame_index
next_snapshot_revision
next_log_sequence
authority_mode
```

There is no FIFO, priority queue, task table, result table, event table, or retry table.

### 12.2 Admission linearization point

`trySchedule(...)` is the only producer mutation entry. Its linearization point is the
atomic/mutex-protected construction of `pending_mutation`.

Bounded JSON parsing and preliminary conversion may happen before taking the admission
lock. Immediately before constructing `pending_mutation`, `trySchedule(...)` must
atomically re-read and validate, under one `OptionSystem` synchronization boundary:

- lifecycle and producer authority;
- admission-gate state;
- binding epoch/token;
- catalog and schema identity;
- shared published availability;
- pending-slot vacancy.

Graph catalog replacement uses the same synchronization boundary. This prevents a
time-of-check/time-of-use race between catalog validation and slot reservation.

The pending record stores copied typed input plus stable option ID, binding epoch, graph
generation, origin, and correlation metadata. It never stores JSON, a raw node pointer, or
a graph callback captured from a stale lookup.

Before that point, any error is a rejection. After that point:

- the request is started;
- the server may attempt only a success response;
- failure to serialize/send that response does not roll the mutation back;
- disconnecting the client does not cancel the mutation;
- the mutation remains scheduled until a renderable frame attempts it or shutdown
  abandons it with a log.

This rule is required to preserve:

> Any explicit rejection received by a client means the mutation will never execute.

### 12.3 Admission order

The recommended check order is:

1. server/session is running;
2. caller has current write authority;
3. request has exactly one ID and one complete input value;
4. request carries valid binding proof;
5. option exists in the current catalog;
6. value converts and passes static validation;
7. current published availability permits an attempt;
8. admission gate is open;
9. pending slot is empty;
10. assign sequence/correlation metadata and fill the slot.

Steps 1–9 make no domain mutation.

### 12.4 Busy behavior

If the pending slot is occupied:

- the new mutation is rejected immediately with `operation_busy`;
- it is not queued;
- it does not replace the pending value;
- it is not coalesced with a camera update;
- the caller must not automatically retry.

### 12.5 At-most-once execution

At the next renderable frame boundary, the coordinator moves the pending mutation into a
local frame variable exactly once and clears the slot while admission remains closed.

That move is the irreversible attempt boundary. There is no leased-operation state,
restore path, or requeue path. The coordinator never puts a failed operation back into the
slot. Domain failure consumes that frame's one attempt. Automatic retry would violate the
chosen model.

If a frame-coupled operation such as capture or reset is dispatched but the frame exits
without a valid render submission/consumption, a frame-exit finalizer must:

- clear the temporary one-frame signal;
- write terminal `failed_before_submission`;
- preserve any old canonical value that was not validly committed;
- never restore or retry the operation.

### 12.6 Shutdown abandonment

If shutdown begins with a pending mutation:

- admission closes;
- the pending mutation is removed;
- a terminal record with `status=abandoned` and `reason=shutdown` is written to the
  command line;
- no protocol task/result record is created;
- the operation is not replayed on the next process start.

## 13. Frame Timeline

### 13.1 Renderable frame N

The required ordering is:

```text
application iteration
  1. poll presentation events and confirm framebuffer, renderer, and graph are renderable
  2. if not renderable, retain the pending slot and last snapshot; do not create an option
     frame

frame N boundary
  3. close admission gate and move at most one pending mutation to a local variable
  4. revalidate its live generation/binding and execute its one irreversible attempt:
       - an ordinary value commits canonical state only on success
       - model/environment/pipeline execute synchronously in V1
       - capture/reset produces frame N's sole frame effect and does not claim GPU success
  5. collect canonical values and conservative availability
  6. freeze and atomically publish Snapshot N
  7. reopen admission with an empty pending slot

frame N body
  8. in offline-Lua mode, resume the Lua coroutine once against Snapshot N
  9. consume the latest per-poll vertical wheel delta and begin the Dear ImGui frame
 10. render semantic UI from Snapshot N; agent and offline-Lua modes use a disabled
     read-only mirror
 11. in human mode, give a committed UI mutation first chance to reserve N+1
 12. if no UI mutation reserved the slot and UI does not capture input,
     sample and submit one combined camera-pose mutation
 13. renderer, resolution resolver, camera derivation, and all nodes consume Snapshot N
 14. Renderer calls Device::beginFrame(), then advances frame-slot-bound continuations
 15. Present/render submission for frame N proceeds
 16. a renderer frame-scope finalizer closes every frame-coupled operation outcome,
      including all early-return/failure paths
```

Agent-mode WebSocket admission can occur asynchronously after step 7. The first accepted
mutation reserves the slot for a later renderable frame.

### 13.2 Gate reason

Admission remains closed from step 3 through snapshot publication. It must not reopen
immediately after moving the pending operation because:

- the current operation may replace the graph and catalog;
- a request admitted against the old catalog during replacement could capture an invalid
  binding;
- UI/network readers must not observe a half-rebound state.

A request arriving during this short interval is rejected with `admission_closed`; it is
not silently delayed.

### 13.3 One-frame visibility rule

Snapshot N is fixed before any producer in frame N body can submit a mutation. Therefore:

- UI mutation in frame N can first appear in snapshot N+1;
- WASD/mouse input sampled in frame N can first affect camera snapshot N+1;
- Lua resumed against snapshot N can first affect N+1;
- a WebSocket mutation admitted after snapshot N publication can first affect a later
  snapshot.

The accepted one-frame camera latency is the cost of one consistent model.

### 13.4 Minimized or zero-extent presentation

When the framebuffer is unavailable:

- no new option frame index is created;
- no pending mutation is consumed;
- no speculative snapshot is published;
- WebSocket reads return the last published snapshot;
- the pending slot stays occupied;
- further mutations are rejected as busy;
- no Renderer preamble runs and frame-slot-dependent capture harvest does not occur;
- capture completion waits until a later renderable frame, an explicit graph flush, or
  orderly shutdown;
- any terminal completion observed during a flush still changes published availability no
  earlier than the next snapshot.

This avoids inventing a second “logical interaction frame” and prevents capture/reset
requests from disappearing while minimized.

### 13.5 Internal changes that do not consume the slot

The following are not new producer mutations:

- GPU fence completion;
- resource residency/cache updates;
- completion of a previously started capture operation;
- EXR encoding/file write after capture start;
- swapchain/viewport extent synchronization;
- renderer history counters;
- derived camera matrices;
- graph/runtime bookkeeping.

Harvesting an already-started continuation may:

- update derived runtime state and shared domain availability;
- emit the terminal log using the original sequence.

It is attributed to the original operation and is not a new producer mutation, so it
consumes no new frame slot. V1 keeps model, environment, and pipeline mutation synchronous;
capture is the principal retained continuation.

`frame_index` is an independent monotonic `uint64_t` owned by `OptionSystem`. It must never
reuse the RHI `FrameSlot` index, which cycles through the frames in flight.

## 14. Authority and Arbitration

### 14.1 Launch-time modes

V1 uses one immutable launch-time authority mode:

```text
human
agent
offline-lua
```

Changing authority at runtime is not an option in V1. It would add another lifecycle race
without improving the core interaction model.

### 14.2 Human mode

- Dear ImGui is writable.
- Physical camera input is enabled.
- WebSocket, if enabled for diagnostics, is read-only.
- Lua automation is not running.

Within a frame:

1. a committed Dear ImGui edit has priority;
2. only if the UI did not reserve the slot and ImGui does not want keyboard/mouse input
   may the camera adapter submit;
3. camera input produces at most one pose mutation.

This rule prevents a held W key from starving every UI control.

### 14.3 Agent mode

- exactly one authenticated WebSocket controller may mutate;
- Dear ImGui renders the same snapshot as a read-only mirror;
- physical camera input is disabled;
- Lua automation is not running.

One controller connection keeps admission and network ambiguity simple. Additional
observer/controller-session protocols are outside V1.

### 14.4 Offline Lua mode

- one host-owned Lua coroutine is the mutation producer;
- Dear ImGui is a read-only mirror;
- physical camera input is disabled;
- the WebSocket server is normally disabled or read-only.

### 14.5 Same semantics does not mean simultaneous writers

All three surfaces have the same option IDs, schemas, validation, snapshot values, and
`read/apply` semantics. Authority decides which surface may submit now. This does not
create three different option systems.

### 14.6 Presentation-only UI navigation

Physical mouse-wheel input is not a fourth mutation producer. It has no option ID,
`MutationOrigin`, binding proof, admission-slot interaction, or one-frame snapshot
visibility rule.

`PresentationContext` accumulates only finite vertical offsets delivered during the
latest GLFW event poll. `UiSystem::beginFrame()` consumes that value once and submits it
to Dear ImGui with a zero horizontal component before `ImGui::NewFrame()`. This path is
active in human, agent, and offline-Lua modes so both interactive UI and read-only mirrors
remain vertically navigable. Horizontal offsets are discarded, unconsumed offsets from
older non-renderable iterations are cleared before the next event poll, and Dear ImGui
window scaling remains disabled.

The wheel delta is never exposed to `AppCamera`, `OptionUiPresenter`, `OptionSystem`, the
WebSocket protocol, or the Lua host. In particular, it cannot zoom or otherwise mutate
the viewer camera, edit an option, reserve the mutation slot, or trigger an automation
operation.

## 15. Camera Design

### 15.1 Canonical options

Recommended initial camera options:

| ID | Complete value |
|---|---|
| `viewer.camera.pose` | position, yaw degrees, pitch degrees |
| `viewer.camera.vertical_fov_degrees` | integer degrees in `[1, 179]` |
| `viewer.camera.clip_planes` | near/far pair with `near >= 0.001` and `far > near` |
| `viewer.camera.movement_speed` | finite renderer world units per second in `[0.01, 1000]` |

Viewport extent remains derived from presentation and is not a mutation.

### 15.2 Human input adapter

[`AppCamera`](../src/app/nrAppCamera.ixx) keeps:

- cursor-position tracking;
- input sampling;
- delta-time sanitization;
- movement and look math;
- UI capture checks.

It no longer directly mutates `ViewerPerspectiveCamera`.

For frame N it:

1. reads pose and movement speed from snapshot N;
2. samples W/S/A/D/Q/E and the current cursor position;
3. updates the cursor baseline for this frame even if UI capture, UI priority, or a busy
   slot suppresses camera mutation;
4. discards any suppressed mouse delta instead of accumulating it;
5. when camera input is permitted, applies the existing movement/look math to a temporary
   camera value;
6. if the value changed, submits one complete `viewer.camera.pose` mutation with snapshot
   N's binding epoch/token;
7. if another mutation already owns the slot, drops this input sample without queuing it.

All movement axes and mouse rotation for that sample are one atomic pose operation.
There is no local/internal `trySchedule` overload that omits binding proof.

### 15.3 Direct agent/Lua camera control

WebSocket and Lua write the same `viewer.camera.pose` and
`viewer.camera.movement_speed` options used by human input. There is no dedicated
`camera.set_pose` or movement-speed mutation method.

There is also no separate `camera.look_at` mutation path. A look-at helper, if useful, may
be provided as a pure client/SDK calculation that produces the complete pose value. It
must not bypass `option.apply`.

### 15.4 Coordinate contract

The initial schema publishes:

- position units are renderer world units;
- the view is right-handed with positive Y as world up;
- yaw and pitch use degrees;
- yaw increases from +X toward +Z and is normalized to `[-180, 180)`;
- pitch is clamped to `[-89, 89]`;
- movement speed is measured in renderer world units per second and is constrained to
  `[0.01, 1000]`;
- all numeric components must be finite and representable by the renderer's 32-bit
  floating-point camera state.

The binding converts degrees to the current renderer's radian-based
`ViewerCameraPose`. This conversion is centralized and identical for UI, WebSocket, and
Lua.

### 15.5 Logging cost

A held camera input may produce one completed mutation log per renderable frame. At 60
FPS, this is approximately 60 compact records per second. The implementation must keep the
record single-line and compact. The bounded asynchronous sink writes these records to
`options.ndjson`, so normal camera activity does not fill the command window or require a
launcher to drain output pipes.

## 16. Domain Execution Patterns

### 16.1 Ordinary value

Examples: UI opacity, bounce count, Russian roulette, FOV.

At the frame boundary:

1. revalidate domain invariants;
2. prepare/rebuild candidate derived runtime without publishing it;
3. commit the candidate runtime and canonical active value together;
4. publish the new value in the same frame snapshot;
5. write one compact success log.

On any failure before the atomic commit, the old runtime and active value remain and one
failure log is written. If preparation would create irreversible side effects, the
executor must either provide an explicit rollback or prove that every step after the
commit point cannot fail.

### 16.2 One-frame signal

Examples: DLSS history reset.

At the frame boundary:

1. dispatch one internal signal for this same renderable frame;
2. the target node consumes it once;
3. retain only the minimum consumed serial/marker needed to prevent duplicate use;
4. write the outcome log;
5. do not create a public task or persistent result.

The internal marker is renderer correctness state, not an externally queryable operation
record. A frame-local `FrameEffectSink` enforces the exact submission boundary:

1. the snapshot carries at most one effect sequence, ID, and typed input;
2. the target node must claim that effect and bind it to a concrete `GraphPassHandle`;
3. the compiler maps that pass to a concrete submit-batch index;
4. the executor reports the exact batch indices accepted by queue submission;
5. a RAII finalizer defaults to `failed_before_submission` and succeeds only when the
   target batch—not an unrelated batch—was accepted.

`rendered`, `recorded`, or `submittedBatchCount > 0` are not valid substitutes for the
exact target-batch result. DLSS reset succeeds when the DLSS evaluation batch is accepted.
Capture dispatch succeeds when the image-to-readback copy batch is accepted; only then may
Present arm its real in-flight continuation.

### 16.3 Long operation

The V1 long-operation example is capture. Model, environment, and pipeline mutations
remain synchronous at the frame boundary.

The initial frame execution may:

- synchronously finish, or
- successfully dispatch domain-owned continuation work.

After dispatch:

- the global option slot is free for later frames;
- the same domain option may become unavailable while its continuation is active;
- unrelated options may continue;
- completion is harvested at a later frame boundary without consuming a slot;
- final success/failure is logged;
- no cancellation or result query is exposed.

At harvest, the Renderer clears capture in-flight state, writes the file/terminal record,
and lets the next option snapshot publish the resulting availability. A worker never
commits option state directly.

### 16.4 Domain concurrency

The global one-slot rule does not by itself make all long operations mutually compatible.
Each domain must publish conservative availability.

Recommended V1 rules:

- only one Present capture may be in flight;
- graph replacement is unavailable while a destructive model/environment transition
  cannot be safely finalized;
- a second load of the same domain is unavailable until the first finishes;
- camera and ordinary scalar changes may continue while an independent capture write is
  progressing;
- shutdown and graph teardown take precedence over accepting new work.

There is no wait queue when a domain is unavailable.

## 17. Fixed Initial Option Catalog and Domain Mapping

The following catalog is normative for V1. Session options survive graph replacement;
graph options are re-created from their declared defaults only after a successful graph
replacement.

### 17.1 Session scope

| ID | Closed schema and constraints |
|---|---|
| `viewer.pipeline.selected` | current registry ID; initially `normalview` or `rtobject` |
| `viewer.model.source` | root-relative UTF-8 path below `nr::projectRoot/assets` |
| `viewer.environment.source` | one extension-free name from the startup-scanned direct `.exr` children of `assets/envMap` |
| `viewer.rt.post_processing_mode` | `accumulate` or `dlss_ray_reconstruction` |
| `viewer.window.fullscreen` | boolean |
| `viewer.exit` | closed empty-object frame effect; completes the current renderable frame, then shuts down the viewer |
| `viewer.camera.pose` | closed `{position:[x,y,z], yaw_degrees, pitch_degrees}` with the coordinate/range and 32-bit representability contract in §15.4 |
| `viewer.camera.vertical_fov_degrees` | integer degrees in `[1, 179]` |
| `viewer.camera.clip_planes` | closed `{near, far}` with finite, 32-bit-representable values, `near >= 0.001`, and `far > near` |
| `viewer.camera.movement_speed` | finite number in `[0.01, 1000]`, default `3.5` renderer world units per second |

`viewer.window.fullscreen` is the only fullscreen mutation path. `UiNode` and presentation
code must not expose a direct `setFullscreen` bypass.

`viewer.exit` is the only option-driven shutdown path. Dear ImGui, WebSocket, and Lua
discover and schedule the same session effect; the frame coordinator writes its terminal
success record, completes that renderable frame, then stops the interaction hosts and
shuts down `AppSession`.

### 17.2 Graph scope

| ID | Closed schema and default |
|---|---|
| `render.path_tracing.max_surface_bounces` | unsigned integer `1..64`, default `16` |
| `render.path_tracing.russian_roulette_enabled` | boolean, default `true` |
| `render.path_tracing.filter_after_shading_enabled` | boolean, default `false`; a successful transition resets temporal history |
| `render.accumulate.max_history_samples` | unsigned integer `1..4096`, default `1024` |
| `render.dlss.enabled` | boolean, default `true` |
| `render.dlss.quality` | `performance`, `balanced`, `quality`, `ultra_performance`, or `dlaa`; launch `--dlss-quality` seeds this value |
| `render.dlss.bypass` | boolean, default `false`; legal only while quality is `dlaa` |
| `render.dlss.visualize_motion_vectors` | boolean, default `false` |
| `render.dlss.reset_history` | closed `{}` one-frame effect |
| `render.present.tone_mapping` | `auto`, `none`, `reinhard`, `aces_filmic`, or `bt2390_eetf` |
| `render.present.ui_opacity` | finite number `0..1`, default `1` |
| `render.present.capture_exr` | closed `{}` one-frame effect |

There is no `viewer.rt.dlss_quality`. Cross-option constraints reject rather than
implicitly repair values: with bypass true, changing quality to a non-DLAA mode returns
`invalid_params`; bypass must be cleared in an earlier frame.

### 17.3 Node migration and singleton rule

- PathTracing derives its structural variant from the two path-tracing keys in snapshot N
  and removes `variantUiDraft_`/`pendingVariant_`.
- Accumulate reads `render.accumulate.max_history_samples` and removes its draft/pending
  fields; history images and validity remain runtime state.
- DLSS resolution planning, structural snapshot, skeleton patch, materialization, and
  build all read the same snapshot. `uiDraft_` and `pendingInput_` are removed; NGX runtime
  and the minimum effect-consumption state remain.
- Present reads tone mapping and opacity from the same snapshot and removes their
  draft/pending fields.
- Actionable PathTracing, Accumulate, DLSS, and Present semantic node types are singletons
  in one graph. Preflight rejects duplicates before old-graph teardown.

### 17.4 Transactional model replacement

`AppSession` changes its scene owner from `optional<Scene>` to `unique_ptr<Scene>` so a
detached candidate can coexist with the active scene:

1. decode the requested model;
2. construct a detached candidate `Scene`;
3. complete template registration and instance creation on the candidate;
4. if any step fails, destroy only the candidate and preserve the old Scene, model value,
   and all camera values;
5. at the frame boundary, reset the renderer's old scene binding, swap the candidate into
   ownership, and destroy the old Scene;
6. atomically commit the model source plus pose, FOV, and clip planes derived from the new
   scene primary camera or the fallback camera.

The camera reset is an explicit derived commit of the original
`viewer.model.source` mutation. It consumes no second mutation slot and emits no second
result record.

### 17.5 Filesystem roots

```text
assets:      nr::projectRoot/assets
automation:  nr::projectRoot/automation
captures:    nr::projectRoot/screenshots
```

Every input path is canonicalized and then compared by path component with its fixed root.
Reject `..` escape, UNC/device paths, URLs, root-external symlinks, and shell syntax.
Legacy model history entries outside the asset root are ignored.

### 17.6 EXR capture

`render.present.capture_exr` uses `{}` and returns only admission. At the irreversible
attempt boundary:

1. reserve provisional Present busy state and conservatively publish capture unavailable;
2. when Present materializes that frame effect, generate the path exactly once as
   `screenshots/<session_id>/capture_<sequence>_frame_<frame_index>.exr`;
3. place the effect in snapshot N and require Present to claim its copy pass;
4. emit the dispatch-started machine record only after the copy batch is accepted;
5. only then arm Present's `screenshotPendingSave_` continuation.

If the target copy batch is not accepted, the frame finalizer logs
`failed_before_submission`, clears provisional state, and never retries or requeues. An
unrelated submitted batch cannot make capture succeed.

Capture completion is intentionally Renderer-owned. `Device::beginFrame()` first waits for
and reclaims the owning RHI frame slot; the Renderer preamble then advances the Present
continuation, synchronously writes EXR, and emits the terminal machine record. The current
snapshot was already frozen, so capture availability changes no earlier than the next
snapshot. Minimized iterations do not call this hook; harvest waits until rendering
resumes, graph replacement flushes the graph, or orderly shutdown flushes it.

Present retains only the readback buffer, pending-save metadata, frame-slot association,
chosen path, original correlation fields, and real provisional/in-flight state. Multi-
request screenshot counters and retained screenshot result/status UI are removed. The
protocol never returns capture path or bytes.

Before destroying Present, graph replacement and shutdown wait/flush a submitted capture,
or terminally fail a provisional effect that never reached submission. No accepted capture
may disappear silently.

## 18. Graph Replacement Boundary

Graph selection consumes the one mutation slot for that frame.

The recommended V1 graph-switch sequence is synchronous:

1. close admission and detach the selected-pipeline mutation;
2. validate the pipeline ID and build a complete candidate catalog from pure node
   declarations;
3. preflight node/submit indices, semantic singletons, option ID/schema/default validity,
   resolver-required keys, graph option scope, and the serialized snapshot size;
4. only after preflight succeeds, cross the destructive barrier: wait/flush the old graph
   and capture continuation, then tear down old runtime;
5. initialize the new graph;
6. install its nodes and runtime hooks;
7. atomically replace the data-only graph catalog, graph defaults, active pipeline ID,
   incremented
   `graph_generation`, and incremented `binding_epoch`;
8. freeze and publish the new snapshot;
9. reopen admission.

Reads during the barrier continue to see the old immutable snapshot until step 8.

The switch must not publish a new pipeline ID while the old graph is still active.

V1 fixes the failure boundary as follows:

- failure discovered during candidate validation/preflight leaves the old graph and active
  pipeline value unchanged;
- failure after the old graph has crossed its destructive uninstall boundary is logged and
  fails fast through the project's normal fatal-error path;
- V1 does not implement a general graph rollback/reinstall framework.

Continuing with a snapshot that claims a graph is active when no corresponding runtime
exists is forbidden.

## 19. WebSocket Transport

### 19.1 Standards and dependencies

Implemented dependency stack:

- WebSocket framing/handshake: Boost.Beast over Boost.Asio;
- JSON parsing/serialization: Boost.JSON;
- application envelope: JSON-RPC 2.0;
- text messages only.

The vcpkg `builtin-baseline` is pinned to the 2026.06.24 commit
`cd61e1e26a038e82d6550a3ebbe0fbbfe7da78e3`; Boost.Asio, Boost.Beast, and
Boost.JSON are pinned at 1.91.0 through the dependency boundary.

[Boost.Beast](https://www.boost.org/library/latest/beast/) supplies HTTP/WebSocket
operations over Asio. [RFC 6455](https://www.rfc-editor.org/rfc/rfc6455.html) requires
implementations to protect their frame and reassembled-message limits. Raw Asio/Beast
headers remain behind `dependency.network`; raw Boost.JSON headers remain behind
`dependency.json`.

### 19.2 Listener and authentication

V1 must:

- use `--interaction human|agent|offline-lua`, defaulting to `human`;
- in agent mode, read a 32-byte base64url bearer token only from
  `NR_OPTION_BEARER_TOKEN`;
- bind exactly `127.0.0.1:0`, never IPv6, remote interfaces, or TLS;
- publish the selected non-secret endpoint in one `NR_OPTION_ENDPOINT_V1` machine record;
- accept only the exact HTTP target `/v1/options`;
- validate the token during the HTTP Upgrade;
- never accept or expose the token through argv, query parameters, or logs;
- permit exactly one writable connection in agent mode;
- reject every handshake containing an `Origin` header;
- disable WebSocket compression in V1.

Loopback reduces exposure but is not authentication.

Only one authenticated connection exists at a time. After disconnect, a new controller
may authenticate and reconnect; disconnect never cancels an already admitted mutation or
capture continuation.

### 19.3 Threading

Use asynchronous I/O on a dedicated network execution context. A connection object owns:

- the Beast stream;
- bounded read buffer;
- one preallocated bounded response slot;
- authentication/session state;
- JSON-RPC request-ID bookkeeping for currently outstanding responses only.

It owns no renderer state.

### 19.4 Bounded resources

Recommended initial configurable limits:

| Limit | Recommended V1 default |
|---|---:|
| writable connections | 1 |
| reassembled text message | 256 KiB |
| single serialized JSON response | 256 KiB |
| JSON nesting depth | 32 |
| HTTP headers | 8 KiB |
| HTTP target | 256 bytes |
| request ID string | 128 bytes |
| option ID length | 128 bytes |
| ordinary string input | 4 KiB unless the option declares less |
| request rate | 240/s with burst 60 |
| inbound bytes | 4 MiB/s with burst 512 KiB |
| outbound bytes | 16 MiB/s with burst 1 MiB |
| handshake timeout | 5 seconds |
| idle/pong policy | ping after 15 seconds idle; close after 45 seconds without pong |
| binary message | rejected |
| permessage-deflate | disabled |

Read rate limits are a transport defense, not a frame semantic limit. Binary, invalid
UTF-8, and oversized messages close with RFC 6455 codes 1003, 1007, and 1009 respectively.

V1 has no snapshot pagination. Catalog installation must verify that the maximum
serialized snapshot, including bounded current values, fits the configured outbound
response limit. An oversized catalog fails registration instead of becoming partially
discoverable.

### 19.5 JSON-RPC profile

The protocol uses [JSON-RPC 2.0](https://www.jsonrpc.org/specification) with a deliberately
small profile:

- named object parameters only;
- one JSON-RPC request object per WebSocket text message;
- no JSON-RPC batch arrays;
- no mutating notification;
- client request IDs must be strings or non-fractional integers;
- a client should not reuse an ID while it remains outstanding.

One connection is lockstep: it does not read the next request until the current response
has been completely written. It does not run two handlers concurrently, send unsolicited
events, accept binary/compression, or maintain an outbound response queue.

JSON-RPC notifications have no response, so they cannot confirm `started` or `rejected`.
An `option.apply` object without `id` must not be scheduled.

JSON-RPC batch processing may be concurrent and response ordering is not guaranteed by the
base specification. Disabling it also prevents a batch from pretending to be a supported
multi-option transaction.

The fixed `started` response is serialized and its write capacity reserved before calling
`trySchedule(...)`. After the slot is reserved, the connection may only attempt that
started response. A send failure closes the connection and logs transport loss; it must
not roll back the mutation or substitute an error response.

## 20. WebSocket Application Protocol

### 20.1 Methods

V1 exposes only:

```text
session.describe
option.snapshot
option.get
option.apply
```

There are no camera-specific methods, action methods, task methods, cancellation methods,
result methods, or event-subscription methods.

### 20.2 `session.describe`

Request:

```json
{
  "jsonrpc": "2.0",
  "id": "describe-1",
  "method": "session.describe",
  "params": {}
}
```

Conceptual result:

```json
{
  "jsonrpc": "2.0",
  "id": "describe-1",
  "result": {
    "protocol": "newbie-renderer-options",
    "version": 1,
    "authority_mode": "agent",
    "writable": true,
    "mutation_model": "single-slot-next-renderable-frame",
    "final_result_channel": "rotating-ndjson-file",
    "final_result_path": "C:/path/to/Newbie-Renderer/build/app/logs/options.ndjson",
    "final_result_schema": "NR_OPTION_V1",
    "capture_format": "exr",
    "tasks": false,
    "batch_mutation": false
  }
}
```

`final_result_path` is the absolute generic-form path produced from the configured project
root. The path above is illustrative; clients must use the returned value rather than
reconstructing it from their working directory.

### 20.3 `option.snapshot`

Request:

```json
{
  "jsonrpc": "2.0",
  "id": "snapshot-1",
  "method": "option.snapshot",
  "params": {}
}
```

Conceptual result:

```json
{
  "jsonrpc": "2.0",
  "id": "snapshot-1",
  "result": {
    "frame_index": 812,
    "snapshot_revision": 813,
    "graph_generation": 4,
    "binding_epoch": 19,
    "snapshot_token": "opaque-session-binding-token",
    "options": [
      {
        "id": "render.present.ui_opacity",
        "title": "UI opacity",
        "description": "Opacity of the composed UI layer.",
        "input_schema": {
          "type": "number",
          "minimum": 0.0,
          "maximum": 1.0
        },
        "value": 1.0,
        "available": true
      }
    ]
  }
}
```

The actual schema must be closed and versioned. The example omits presentation metadata
for brevity.

### 20.4 `option.get`

Request:

```json
{
  "jsonrpc": "2.0",
  "id": "get-1",
  "method": "option.get",
  "params": {
    "id": "viewer.camera.pose"
  }
}
```

It returns the record from one atomically loaded snapshot together with that snapshot's
frame, epoch, and token. It does not fetch mutable node state.

### 20.5 `option.apply`

Epoch form:

```json
{
  "jsonrpc": "2.0",
  "id": "apply-42",
  "method": "option.apply",
  "params": {
    "id": "render.present.ui_opacity",
    "value": 0.8,
    "binding_epoch": 19
  }
}
```

Token form:

```json
{
  "jsonrpc": "2.0",
  "id": "apply-43",
  "method": "option.apply",
  "params": {
    "id": "render.present.capture_exr",
    "value": {},
    "snapshot_token": "opaque-session-binding-token"
  }
}
```

Only successful admission returns:

```json
{
  "jsonrpc": "2.0",
  "id": "apply-43",
  "result": {
    "status": "started"
  }
}
```

No additional domain result is later sent on this request.

### 20.6 Rejection

Example:

```json
{
  "jsonrpc": "2.0",
  "id": "apply-44",
  "error": {
    "code": -32011,
    "message": "Operation was not started.",
    "data": {
      "reason": "operation_busy"
    }
  }
}
```

Stable reasons:

| JSON-RPC code | Stable reason | Meaning |
|---:|---|---|
| `-32602` | `invalid_params` | malformed schema, missing binding proof, invalid type/range, extra field |
| `-32602` | `unknown_option` | option ID does not exist in the current binding |
| `-32010` | `controller_unavailable` | caller is not the active mutation authority |
| `-32011` | `operation_busy` | the global pending slot is occupied |
| `-32012` | `admission_closed` | frame/graph/shutdown critical section currently owns the gate |
| `-32013` | `stale_binding` | epoch/token does not identify the live catalog binding |
| `-32014` | `option_unavailable` | option exists but current domain state cannot start it |
| `-32015` | `snapshot_not_ready` | initial snapshot has not been published |
| `-32016` | `server_stopping` | shutdown has begun |
| `-32017` | `rate_limited` | connection resource/rate limit was exceeded |

Unknown option IDs use `-32602` with `data.reason = "unknown_option"`.

### 20.7 Network ambiguity

Three cases must remain distinct:

| Observation | Meaning |
|---|---|
| `result.status == "started"` received | slot was reserved |
| explicit error received | slot was not reserved and request will not execute |
| connection closed/timeout before a response | unknown; request may or may not have reserved the slot |

The client must not automatically resend the third case. If a human or higher-level agent
chooses to issue another mutation, it is a new attempt and may duplicate the original
domain intent.

The JSON-RPC request `id` is only response/log correlation. It is not a durable operation
identity and does not provide deduplication after reconnect.

Recommended agent pacing is:

1. read a snapshot;
2. send one mutation once;
3. if it starts, observe `build/app/logs/options.ndjson` and/or wait for `frame_index` to
   advance before deliberately issuing another independent mutation;
4. never treat a timeout as permission to replay the same mutation automatically.

Reads may continue at any point within transport limits.

## 21. Rotating NDJSON Log Contract

### 21.1 Role

The rotating `build/app/logs/options.ndjson` file is the only final outcome channel for a
started mutation. Its format, fixed discovery path, and rotation behavior are therefore
part of the interaction contract, not incidental diagnostics.

All project records still go through `nr.utils:errorHandle`. Its rotating NDJSON sink is
an implementation of the existing facility, not a module-local semantic logger or result
database.

### 21.2 Required fields

Each mutation outcome record is one stable JSON line:

```json
{"schema":"NR_OPTION_V1","level":"INFO","timestamp_unix_ns":1785177600000000000,"sequence":184,"origin":"websocket","option_id":"render.present.ui_opacity","phase":"terminal","status":"succeeded","frame_index":813}
```

Each successful agent-mode WebSocket host start emits exactly one discovery record in the
same stream. It contains the loopback endpoint and never contains the bearer token:

```json
{"schema":"NR_OPTION_ENDPOINT_V1","level":"INFO","timestamp_unix_ns":1785177600000000000,"endpoint":"ws://127.0.0.1:49152/v1/options"}
```

`nrCompactRecord` merges its compact JSON object with `schema`, `level`, and
`timestamp_unix_ns`, producing one valid NDJSON object without a text prefix. The option
object contains `sequence`, `option_id`, `phase`, `status`, `frame_index`, `origin`, and
optional bounded `request_id` and `reason` fields. The frame index is the OptionSystem
renderable-frame attempt ordinal, not the recycled RHI frame slot. Request IDs are
bounded to 128 bytes and reasons to 4 KiB; an internal producer that exceeds a bound is
replaced by a stable overflow sentinel rather than producing an unbounded record. Invalid
UTF-8 in either optional field is likewise replaced by a stable field-specific sentinel
before Boost.JSON serialization.

`sequence` is a process-local monotonic log correlation value. It is not returned as a
task ID and is not queryable. An asynchronous domain may retain only this correlation
value with its genuinely required in-flight state; it discards it after the terminal log.
`OptionSystem` does not retain a completed-operation record.

`origin` always remains the producer that submitted the mutation. Continuation records
retain that original origin.

### 21.3 Record policy

Record policy is fixed:

- synchronous value/camera mutation writes one terminal `succeeded` or `failed` line at
  frame execution;
- capture writes `phase=dispatch status=started` only after its copy batch is accepted and
  one terminal `succeeded`, `failed`, or `abandoned` line later;
- no second “admitted” line is required for an ordinary synchronous mutation;
- every terminal line must fit on one line and flush through the existing facility.

Conceptual examples:

```json
{"schema":"NR_OPTION_V1","level":"INFO","timestamp_unix_ns":1785177600000000000,"sequence":184,"option_id":"render.present.ui_opacity","phase":"terminal","status":"succeeded","frame_index":813,"origin":"websocket","request_id":"apply-42"}
{"schema":"NR_OPTION_V1","level":"INFO","timestamp_unix_ns":1785177600000000001,"sequence":185,"option_id":"viewer.camera.pose","phase":"terminal","status":"succeeded","frame_index":814,"origin":"camera"}
{"schema":"NR_OPTION_V1","level":"INFO","timestamp_unix_ns":1785177600000000002,"sequence":186,"option_id":"render.present.capture_exr","phase":"dispatch","status":"started","frame_index":815,"origin":"websocket","request_id":"apply-43"}
{"schema":"NR_OPTION_V1","level":"INFO","timestamp_unix_ns":1785177600000000003,"sequence":186,"option_id":"render.present.capture_exr","phase":"terminal","status":"succeeded","frame_index":815,"origin":"websocket","request_id":"apply-43"}
```

JSON parsing, escaping, and serialization are delegated to Boost.JSON through
`dependency.json`; project code does not maintain a second textual JSON reader or writer.
This is the repository-wide C++ JSON boundary, not a transport-only facility. Machine
records contain no ANSI color and no source/function continuation line.
The sink writes and flushes complete records from one bounded writer queue. This is a
compact-record entry on the existing compile-time `LogLevel` facility, not a second logger.

### 21.4 Files, rotation, and console routing

One active viewer owns these fixed paths:

| Stream | Active file | Records |
|---|---|---|
| Engine | `build/app/logs/engine.ndjson` | `NR_LOG_V1` diagnostics from ordinary `nrLog` / `nrInfo` / `nrVulkan` / assertions |
| Options | `build/app/logs/options.ndjson` | every `nrCompactRecord`, including `NR_OPTION_V1` results and `NR_OPTION_ENDPOINT_V1` discovery |

The viewer atomically creates an empty `.active-viewer` directory before touching either
active file. If the lease already exists, a second viewer fails before rotation. After an
abnormal process termination, an operator may remove a stale lease only after confirming
that no viewer still owns the directory. On successful viewer startup, any non-empty
active file becomes its `.1` history before the new active segment is created. Each
stream rotates independently at 32 MiB and retains four prior segments:
`engine.1.ndjson` through `engine.4.ndjson` and `options.1.ndjson` through
`options.4.ndjson`.

Every active or retained segment begins with an `NR_LOG_SESSION_V1` object containing the
session ID, stream, active path, timestamp, 32 MiB limit, and retained-segment count. An
agent or human may read or tail the active file directly, but an open handle must not be
assumed to follow rotation. A live reader must detect replacement/truncation, reopen the
active path, and scan the new segment beginning at its session marker; it may scan the
numbered histories when it needs records that crossed the rotation boundary.

The console policy is deliberately asymmetric:

- generic informational diagnostics go only to `engine.ndjson`;
- warnings, errors, and assertions go to `engine.ndjson` and remain visible in the command
  window;
- compact option and endpoint machine records go only to `options.ndjson`;
- if the NDJSON session cannot accept a record, `errorHandle` mirrors that record to the
  console as a last-resort fallback.

The asynchronous queue is bounded. Producers wait for queue space instead of dropping
records, and orderly shutdown drains and flushes accepted records before closing the
files. stdout/stderr capture and continuous process-pipe draining are not required for
agent observability.

Terminal logging is guaranteed only while execution remains inside orderly renderer
control flow. A process crash, `TerminateProcess`, power loss, or unrecoverable logging
failure may leave a caller that received `started` without a terminal record. Because V1
has no durable receipt/task store, that outcome remains permanently unknown and is never
replayed after restart.

## 22. Dear ImGui Presenter

### 22.1 Presentation

`OptionUiPresenter`:

- reads snapshot N;
- creates temporary ImGui display values from snapshot N;
- uses `presentation_hint` only to select widgets/layout;
- submits through the same `trySchedule(...)` entry;
- never writes node fields directly;
- never displays speculative pending values as active.

### 22.2 Commit timing

To avoid consuming the slot for every intermediate slider pixel:

- checkbox/button/combo commits on the discrete user action;
- slider, drag, and input drafts stay presenter-local and commit on Enter or deactivation;
- an edit that has not committed is only local ImGui editing state and is discarded or
  refreshed from the next snapshot according to widget policy.

If several widgets produce commit events in one ImGui frame, the first event in
deterministic presenter traversal order that reaches `trySchedule(...)` may start. Every
later event is rejected; the presenter never merges them.

### 22.3 Busy state

After a UI mutation reserves the slot:

- all stateful mutation widgets remain disabled until a newer snapshot arrives;
- read-only values continue rendering;
- the UI keeps showing the active snapshot value;
- the next snapshot determines whether the value actually changed;
- final truth remains the `NR_OPTION_V1` record in `options.ndjson`.

An immediate admission rejection discards the transient draft and restores the displayed
snapshot value.

### 22.4 Required frame-loop change

The semantic widgets must be built after `UiSystem::beginFrame()` but before physical
camera submission. The current pattern where node callbacks execute later inside the UI
render node cannot provide deterministic UI-before-camera arbitration and must be
restructured.

ImGui draw-data finalization may still happen in the UI render path; semantic option
registration and mutation submission may not.

## 23. Embedded Lua

### 23.1 Runtime

`dependency.lua` uses Lua 5.5.0 source with SHA-256
`57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d`. A project
vcpkg overlay port applies every fix listed on the official Lua 5.5.0 bug page; V1 does
not use the 5.5.1 release candidate or silently upgrade the dependency.

One host-owned Lua state and one host-owned automation coroutine run in
`offline-lua` mode.

The script argument is a root-relative `.lua` path under
`nr::projectRoot/automation`, canonicalized by the common filesystem policy, and the host
loads only a text chunk no larger than 256 KiB.

### 23.2 Frame scheduling

Lua resumes once at the safe point after snapshot N publication. It may:

- read snapshot N;
- attempt one mutation for a later frame;
- perform bounded pure computation;
- call `frame.next()` to yield.

On the next renderable frame the host resumes it again. Lua must never busy-wait on the
render thread.

If minimized, `frame.next()` remains suspended until the next renderable frame; the host
does not invent automation-only frames.

A normal script return completes the current renderer frame and then exits with status 0;
the host does not add a hidden drain frame. Therefore a final `nr.options.apply(...)`
executes only if the script subsequently calls `nr.frame.next()`. A still-pending final
apply is logged as abandoned during shutdown. Script, quota, or conversion failure performs
orderly shutdown and exits nonzero.

Only frame resume/admission ordering is deterministic. V1 makes no bitwise-determinism
promise for `pairs`, `next`, or math functions across runs, library builds, or platforms.

### 23.3 Lua API

Recommended positive host API:

```text
nr.options.snapshot()
nr.options.get(id)
nr.options.apply(id, value, binding)
nr.frame.next()
nr.log.info(text)
```

`nr.options.apply` returns only admission:

```text
true
```

or:

```text
false, stable_rejection_reason
```

It never returns final domain success, capture path, effective value, or a task handle.

Conceptual script:

```lua
local snapshot = nr.options.snapshot()

local started, reason = nr.options.apply(
    "render.present.ui_opacity",
    0.8,
    { snapshot_token = snapshot.snapshot_token }
)

if not started then
    error("option was not started: " .. reason)
end

nr.frame.next()
```

A second `apply` before the first is consumed will receive `operation_busy`.

### 23.4 True function allowlist

Do not call `luaL_openlibs()` and then try to remove dangerous names.

The [Lua 5.5 manual](https://www.lua.org/manual/5.5/manual.html#luaL_openselectedlibs)
provides `luaL_openselectedlibs` for selected library loading, but V1 needs a narrower
per-function policy:

1. load only required library implementations into host-private tables;
2. construct a fresh hidden allowlist table;
3. copy only explicitly approved individual functions;
4. recursively wrap every namespace in a read-only proxy whose hidden backing table is
   unreachable;
5. expose an empty proxy `_ENV` whose `__index` points to the hidden allowlist and whose
   `__newindex` rejects writes;
6. set Lua string's metatable `__index` to the same approved string proxy, so
   `("x").unlisted_function` cannot reach a library backing table;
7. do not expose raw table mutation or metatable access that can bypass the proxies;
8. keep all unlisted globals absent.

The V1 allowlist is exact:

| Table | Exposed names |
|---|---|
| base | `assert`, `error`, `ipairs`, `next`, `pairs`, `select`, `tonumber`, `tostring`, `type` |
| `string` | `byte`, `char`, `find`, `format`, `gmatch`, `gsub`, `len`, `lower`, `match`, `rep`, `reverse`, `sub`, `upper` |
| `table` | `concat`, `insert`, `move`, `pack`, `remove`, `sort`, `unpack` |
| `math` functions | `abs`, `acos`, `asin`, `atan`, `ceil`, `cos`, `deg`, `exp`, `floor`, `fmod`, `log`, `max`, `min`, `modf`, `rad`, `sin`, `sqrt`, `tan`, `tointeger`, `type`, `ult` |
| `math` constants | `maxinteger`, `mininteger`, `pi` |
| `utf8` | `char`, `charpattern`, `codes`, `codepoint`, `len`, `offset` |
| host | `nr.options.snapshot`, `nr.options.get`, `nr.options.apply`, `nr.frame.next`, `nr.log.info` |

Changing this table is a reviewed sandbox-policy change. “Useful” library members are not
implicitly included.

It must not expose:

```text
_G
package, require
io
os
debug
coroutine
dofile, loadfile, load
collectgarbage
rawget, rawset
getmetatable, setmetatable
math.random, math.randomseed
```

If deterministic random values are later required, provide a separate host-owned seeded
generator with an explicit seed in automation configuration.

### 23.5 Additional sandbox limits

The Lua host must:

- load text chunks only;
- canonicalize the script path under `nr::projectRoot/automation`;
- use a custom allocator capped at 32 MiB per state;
- allow at most 100,000 Lua instructions per resume with a hook interval of 1,000;
- apply a soft 5 ms wall budget per resume;
- cap conversion depth at 16, each table at 4,096 entries, total converted nodes at 16,384,
  and each string at 64 KiB;
- cap each `nr.log.info` call at 1 KiB, each resume at 16 calls and 8 KiB total, remove
  embedded CR/LF, and emit it only under a fixed `LUA` category so script text cannot forge
  an `NR_OPTION_V1` record;
- copy values across the boundary;
- never give Lua a raw pointer/reference/userdata for renderer objects;
- terminate the script on budget violation and log through `errorHandle`.

Any `try`/`catch` required by an external Lua boundary must follow the project's narrow
external-library exception policy; project control flow must not use exceptions as its
general error mechanism.

These controls limit accidental runaway scripts and bound host conversions. They do not
claim to sandbox hostile CPU behavior: automation-root scripts are local trusted inputs.

## 24. Deferred MCP Adapter Constraint

MCP is not implemented in this migration and adds no dependency or build target. This
section constrains only a possible future external sidecar, which remains outside the
renderer process:

```text
MCP client
    |
external adapter/sidecar
    |
renderer WebSocket JSON-RPC
```

Any future adapter must not create semantics that the renderer does not have.

### 24.1 Tool mapping

Future tool mapping:

```text
renderer_session_describe
renderer_option_snapshot
renderer_option_get
renderer_option_apply
```

`renderer_option_apply` accepts exactly one option ID, one complete value, and binding
proof.

### 24.2 No MCP Tasks

The MCP 2025-11-25
[`Tasks`](https://modelcontextprotocol.io/specification/2025-11-25/basic/utilities/tasks)
facility is an experimental durable state-machine and polling/result-retrieval model. That
is deliberately incompatible with this renderer's no-retention decision.

Every mutating tool must declare:

```json
{
  "execution": {
    "taskSupport": "forbidden"
  }
}
```

The
[`Tools`](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)
specification permits this value. The adapter must not advertise task capability for these
calls and must not implement `tasks/list`, `tasks/get`, `tasks/result`, or `tasks/cancel`
on the renderer's behalf.

### 24.3 Tool results

- malformed tool input is reported using normal MCP/JSON-RPC validation behavior;
- renderer admission rejection is an ordinary tool execution error with the stable
  rejection reason;
- renderer admission success returns structured `{ "status": "started" }`;
- no deferred MCP result follows;
- final domain outcome remains in the renderer's rotating `options.ndjson` stream.

### 24.4 Capture

The MCP adapter must not read, embed, transcode, or return the renderer's EXR output. If an
agent needs current visual screen content, it uses a screenshot capability supplied by its
own host environment.

## 25. Security and Robustness

### 25.1 Network

- loopback-only listener;
- bearer authentication before WebSocket upgrade;
- one writable controller;
- bounded handshake/header/message/outbound buffers;
- text UTF-8 JSON only;
- closed schemas and finite numeric checks;
- no compression;
- no arbitrary origin acceptance;
- no secrets in logs;
- clean close on protocol abuse or resource exhaustion.

### 25.2 Filesystem

- model input is root-relative below `nr::projectRoot/assets`; environment input is a
  closed extension-free name discovered from direct `.exr` children of the fixed
  `assets/envMap` prefix;
- model path input is length-bounded, canonicalized, and root-restricted;
- capture output is selected by the renderer under `nr::projectRoot/screenshots`;
- protocol callers do not choose arbitrary capture destinations;
- no shell command construction or execution;
- Lua scripts are loaded only from `nr::projectRoot/automation`.

### 25.3 Renderer

- network/Lua never receive Vulkan/RHI/node handles;
- all domain mutation runs on the application/render thread;
- graph-scoped callbacks are invalidated before teardown;
- stale snapshot tokens reject mutation;
- duplicate IDs/schema mismatches fail graph registration;
- domain failure leaves the previous canonical value unless the renderer enters an
  explicit fail-fast state;
- no silent continuation abandonment.

### 25.4 Denial of service

“Reads are free” is not permission for unbounded memory or CPU use. Protect:

- message size and nesting;
- request rate;
- concurrent request count;
- outbound bytes;
- Lua memory/instructions/time;
- filesystem path enumeration;
- log line size;
- capture concurrency.

## 26. Shutdown and Failure Semantics

Recommended shutdown order:

1. mark `OptionSystem` stopping and close admission;
2. stop accepting WebSocket connections and begin connection close;
3. stop/resume-terminate Lua at its host boundary;
4. log and remove a pending, not-yet-executed mutation;
5. finalize or explicitly fail domain continuations, especially Present capture;
6. destroy graph/nodes/scene/UI/renderer in valid ownership order; the closed
   `OptionSystem` catalog is data-only and cannot call the destroyed graph;
7. destroy `OptionSystem`;
8. drain and close the rotating NDJSON session through its RAII lifetime.

Expected failure rules:

| Failure | Required behavior |
|---|---|
| invalid JSON/schema | explicit pre-start rejection |
| stale epoch/token | explicit pre-start rejection |
| global slot occupied | explicit pre-start rejection |
| response send fails after admission | operation remains started; log transport loss |
| domain validation fails at frame execution | old value remains; terminal failure log; no retry |
| frame-coupled signal has no valid frame submission/consumption | clear it and log `failed_before_submission`; no restore/retry |
| renderer stops before pending execution | terminal abandonment log |
| capture continuation fails | terminal failure log with reason/path context |
| graph preflight fails | keep old graph/value and write terminal failure log |
| graph install fails after destructive uninstall begins | write fatal context and fail fast; never publish a false active state |
| NDJSON queue reaches capacity | producer waits for writer progress; records are not dropped |
| NDJSON write/rotation fails | disable the sink and report the failure through the console fallback |

An abnormal process termination may prevent the terminal record itself. V1 intentionally
does not recover, replay, or resolve that permanently unknown outcome on restart.

## 27. Implementation Mapping

### 27.1 New option core

Implemented source area:

```text
src/options/
  exportModule.ixx
  nrOptionModel.ixx
  nrOptionModel.cpp
  nrOptionSystem.ixx
  nrOptionSystem.cpp
  nrOptionRegistration.ixx
  nrOptionRegistration.cpp
```

The partition split follows the project's large-partition policy: declarations and
templates remain in `.ixx`, while non-template runtime implementation lives in `.cpp`.

### 27.2 App ownership and frame coordination

[`AppSession`](../src/app/nrAppSession.ixx) gains ownership of the option system and the
chosen authority mode.

[`src/pipeline/nrPipeline.cpp`](../src/pipeline/nrPipeline.cpp) changes conceptually:

- remove `ViewerPendingRequests`;
- remove the ability of `processPendingRequests(...)` to execute several changes;
- register viewer/pipeline/model/environment options;
- run the single frame-boundary operation;
- publish the snapshot before UI/camera/render work;
- feed the snapshot into `RendererFrameInput`.

### 27.3 Renderer frame input

[`RendererFrameInput`](../src/renderer/nrRenderer.ixx) and
`NodeFrameParameters` receive the same const snapshot reference.

Renderer graph structural/cache logic and node build logic must use that reference.

### 27.4 UI migration

[`UiSystem`](../src/app/nrAppUi.ixx) remains the ImGui owner. A new app-level presenter
replaces direct semantic state ownership.

Existing app and node controls migrate one at a time. A control is considered migrated
only when:

- its definition is registered once;
- its current value is owned by `OptionSystem`;
- all three surfaces see it;
- UI writes through `trySchedule`;
- renderer/node reads snapshot;
- old draft/pending fields are removed.

### 27.5 Camera migration

[`AppCamera::updateFromPresentation`](../src/app/nrAppCamera.cpp) is split conceptually
into:

- viewport synchronization;
- input sampling;
- pure next-pose calculation;
- option submission.

The direct `viewer_.applyControl(...)` mutation path is removed after snapshot-driven
camera construction works.

### 27.6 Node migration

Recommended order:

1. Accumulate scalar option;
2. PathTracing structural options;
3. Present opacity/tone mapping;
4. DLSS structural/runtime options and reset;
5. Present EXR capture.

This order begins with simple values, then structural values, then one-shot/continuation
semantics.

### 27.7 Network and Lua

Only after the in-process human path uses the final option API:

- add `dependency.json` plus the bounded JSON adapter, then `dependency.network` plus the
  WebSocket adapter;
- add protocol contract tests;
- add `dependency.lua` plus sandbox/scheduler;
- prove both consume the same serialized snapshot/definition model.

This prevents transport code from stabilizing an interim registry.

MCP stays deferred; no MCP dependency, build target, or implementation is part of these
phases.

### 27.8 Architecture documentation

When implementation changes the actual app/renderer/render-pass boundaries, update
`docs/architecture/README.md` and affected topic documents in the same code change. This
design document describes the accepted contract and its implementation phases; the global
architecture document describes the implemented runtime boundary.

## 28. Phased Implementation Plan

### Phase 0: Correct and freeze the contract

- record the model-to-camera derived update, semantic singletons, Renderer-owned capture
  harvest/minimized behavior, and Lua trust/determinism limits;
- freeze the exact catalog in §17, including fullscreen and the removal of
  `viewer.rt.dlss_quality`;
- freeze graph preflight-before-destruction and defer MCP.

Exit condition: no control lacks an owner or frame/lifecycle policy.

### Phase 1: Pure `nr.options` core

- implement definitions, typed values, registration, binding epochs, tokens;
- implement immutable publication;
- implement the one pending slot and admission gate;
- implement renderable-frame consume/execute/collect/freeze;
- add the `NR_OPTION_V1` compact-record entry to `nr.utils:errorHandle`;
- implement unit tests without ImGui, WebSocket, Lua, Vulkan, or concrete nodes.

Exit condition: concurrency tests prove only one simultaneous submit can start.

### Phase 2: Frame-contract vertical slice

- make `AppSession` own the system;
- replace `ViewerPendingRequests`;
- publish initial Snapshot 0 and establish the exact renderable/minimized/shutdown
  ordering;
- require the same snapshot in renderer input, node parameters, and resolution resolver.

Exit condition: the app executes at most one viewer-domain mutation per renderable frame.

### Phase 3: Graph, pipeline, and transactional Scene

- add pure node declaration, semantic-singleton/catalog preflight, and safe graph binding;
- migrate pipeline/post-processing options;
- implement detached Scene candidate/commit ownership and the model-to-camera derived
  commit;
- normalize model paths to the asset root and resolve environment names beneath their
  fixed prefix.

Exit condition: preflight failure preserves the old graph, and model failure preserves the
old Scene and camera.

### Phase 4: Dear ImGui and camera

- add `OptionUiPresenter`;
- implement commit timing and UI-before-camera priority;
- convert combined physical camera input to one pose mutation;
- remove direct viewer/fullscreen mutation bypasses and actionable `NodeUiWriter` paths.

Exit condition: every actionable human control comes from the catalog and camera uses the
same admission slot.

### Phase 5: Node migration and capture

- migrate Accumulate, PathTracing, Present scalar, DLSS, then Present capture;
- remove each node's old draft/pending state in the same change;
- implement exact target-batch effect finalization and Renderer-owned capture harvest;
- enforce one capture in flight and graph/shutdown flush.

Exit condition: no accepted one-shot/long operation silently disappears.

### Phase 6: WebSocket/JSON-RPC

- add loopback listener/authentication;
- implement four methods and stable errors;
- implement bounds/backpressure;
- simulate lost responses and verify no automatic retry/deduplication;
- validate that network callbacks never touch renderer state.

Exit condition: explicit error always means no scheduled mutation.

### Phase 7: Lua

- add pinned Lua and RAII state;
- implement the proxy allowlist environment;
- implement snapshot/get/apply/next-frame;
- enforce memory, instruction, wall-time, path, and conversion limits;
- add negative sandbox tests.

Exit condition: every unlisted function/library is unreachable.

### Phase 8: Cleanup and architecture documentation

- delete obsolete draft/pending/task/event concepts;
- update architecture/topic docs;
- run full LLVM Debug verification required by project policy;
- verify fixed-path discovery, rotation-aware readers, and shutdown draining.

MCP remains deferred after Phase 8. Any later proposal is governed by §24 and must return
for separate implementation approval.

## 29. Verification Strategy

### 29.1 Core unit tests

- duplicate option ID rejection;
- schema/default mismatch rejection;
- epoch/token acceptance and stale rejection;
- both epoch and token supplied but inconsistent;
- only one of many concurrent submissions starts;
- slot-full rejection never overwrites pending;
- gate-closed rejection never queues;
- one pending operation consumed exactly once;
- domain failure is not retried;
- immutable old snapshot remains readable after new publication;
- old graph-scoped definitions cannot authorize mutation after catalog replacement;
- concurrent submit versus graph catalog replacement is linearized under one
  synchronization boundary;
- pending mutation contains copied typed data and no node/callback reference;
- snapshot atomic reads, catalog replacement versus admission, and disconnect versus slot
  reservation preserve their linearization contracts.

### 29.2 Frame tests

- zero mutations in a frame;
- one successful mutation in a frame;
- one failed attempt still consumes that frame's mutation opportunity;
- a submitted mutation never changes the already-frozen current snapshot;
- minimized presentation retains pending work and last snapshot;
- restoring a renderable framebuffer consumes the retained pending mutation once;
- frame-coupled capture/reset fails and clears on every no-submission early return;
- a detached frame attempt is never restored or requeued;
- capture continuation completion changes availability without consuming a new slot;
- snapshot used by renderer and UI has identical frame/revision/token;
- monotonic option frame ordinal remains independent from the cycling RHI frame slot.

### 29.3 Human/UI/camera tests

- UI commit has priority over held movement input;
- ImGui keyboard/mouse capture blocks camera mutation;
- vertical wheel input reaches Dear ImGui before `NewFrame`, discards its horizontal
  component, and has no camera, option, WebSocket, or Lua consumer;
- WASD/QE plus mouse delta produce one pose mutation;
- suppressed mouse input advances/discards the cursor baseline and cannot jump later;
- no physical input produces no mutation;
- camera update appears one snapshot later;
- slider drag produces one commit, not one mutation per pixel/frame;
- read-only agent/Lua-mode UI cannot submit.

### 29.4 Domain tests

- ordinary value commits only on success;
- failed candidate-runtime preparation preserves both old runtime and old canonical value;
- invalid near/far pair fails without state change;
- path-tracing structural key matches snapshot;
- duplicate actionable semantic nodes and candidate preflight failure leave the old graph
  installed;
- old snapshots remain readable but cannot authorize mutation;
- model decode, registration, and instantiation failures each preserve the old Scene and
  camera, while success commits source plus the derived camera reset;
- resolver, node structural snapshot, skeleton patch, materialization, and build read the
  same snapshot;
- DLSS bypass/DLAA constraints, quality, absence of preset option IDs, reset
  target-pass semantics, and resolution consistency;
- DLSS reset is consumed once;
- only the exact target batch can complete a frame effect; unrelated submission,
  compile-only, record-only, and early-return cases fail it;
- only one capture may be in flight;
- capture provisional busy, no-submit cleanup, dispatch/harvest, one-snapshot availability
  delay, minimized delay, graph replacement, shutdown, and EXR write failure;
- graph failure never publishes a false active pipeline;
- domain `available` is identical across surfaces while session writability differs.

### 29.5 Protocol tests

- JSON-RPC 2.0 parse/invalid-request/method/params behavior;
- batch array rejected;
- mutating notification not scheduled;
- success response only after pending-slot commit;
- explicit rejection proves slot was not filled;
- disconnect before response yields no false negative;
- a response lost after slot commit leaves the operation started;
- full snapshot cannot exceed the configured non-paginated response bound;
- message/depth/string/outbound limits;
- authentication, any-Origin rejection, exact target, and IPv4 loopback-only binding;
- fragmented/reassembled message limits, request/byte rate limits, ping/pong timeout,
  notification/batch rejection, and close codes;
- lost started response and reconnect do not cancel the operation;
- network thread cannot access renderer/node APIs.

### 29.6 Rotating-log tests

- all required fields and escaping;
- one compact camera line per completed mutation;
- async start and terminal records use the same process-local sequence;
- async records preserve the original producer origin;
- dispatch uses `status=started`, never terminal `status=succeeded`;
- pending shutdown abandonment log;
- every active/history segment begins with `NR_LOG_SESSION_V1`;
- the 32 MiB default and small test limit produce `.1` through `.4` retention without
  malformed or missing accepted records;
- generic info is absent from the console, warning/error remains mirrored, and compact
  records route only to `options.ndjson`;
- a rotation-aware reader reopens and scans the replacement active file;
- sustained camera logging does not flood stdout/stderr, and orderly shutdown drains all
  accepted records.

### 29.7 Lua tests

- allowed functions work;
- every unlisted library/function is absent;
- recursive namespace proxies cannot be modified or reveal backing tables;
- string metatable lookup cannot reach an unlisted function;
- text-only chunk enforcement;
- script path escape rejection;
- memory/instruction/wall-time limit;
- table/string/depth limits;
- second apply before next frame rejects busy;
- minimized resume, normal/error exit, and final-apply/no-hidden-drain-frame behavior;
- repeated run produces the same option/frame admission trace without promising unordered
  table/math bitwise identity.

### 29.8 Static cleanup checks

The migrated tree must not contain `ViewerPendingRequests`, mutation-style
`NodeUiWriter`, node option draft/pending fields, direct `viewer().applyControl`,
`UiNode`/presentation `setFullscreen`, multi-request screenshot counters, TaskStore,
automatic retry, or a transport-to-renderer mutation path.

### 29.9 Deferred MCP conformance (not part of this implementation)

If the sidecar is approved later, its separate verification must prove that schemas accept
one option only, every mutating tool says `taskSupport: "forbidden"`, no task endpoint is
exposed, admission maps to an ordinary tool result/error, and no EXR/image bytes are
returned. These tests are not an acceptance dependency for the current OptionSystem
migration.

## 30. Acceptance Criteria

The design is implemented only when all statements below are true:

1. One option declaration drives Dear ImGui, WebSocket discovery, and Lua discovery.
2. All three surfaces read the same published immutable snapshot.
3. All three mutation producers call one `trySchedule(...)` entry.
4. No property/action mutation split remains.
5. No `set_many`, batch mutation, patch, or multi-target transaction exists.
6. Across the whole system, at most one new explicit mutation executes in one renderable
   frame.
7. Camera pose and human WASD/mouse-look obey that same limit.
8. Human UI deterministically takes priority over camera input.
9. A mutation cannot affect the snapshot already frozen when it was submitted.
10. Every mutation carries a live binding epoch or snapshot token.
11. `started` means only single-slot admission.
12. Every explicit rejection guarantees no later execution.
13. Network loss remains explicitly unknown and causes no automatic retry.
14. Final success/failure is written as `NR_OPTION_V1` to rotating
    `build/app/logs/options.ndjson` and is not returned as a delayed RPC/Lua result.
15. No TaskStore, task/result/progress/cancel API, or operation receipt ledger exists.
16. Long-operation continuation does not consume later mutation slots.
17. Minimized presentation does not consume pending work.
18. Moving a pending mutation into a frame is irreversible; there is no lease, restore,
    requeue, or hidden retry.
19. A frame-coupled operation without a valid frame submission is explicitly failed and
    cleared.
20. Shared option availability contains no producer-specific authority or live gate state.
21. Renderer, camera, and nodes consume one explicit const frame snapshot.
22. Node-local editable drafts and semantic pending fields are removed after migration.
23. Capture remains EXR-only and returns no pixels/path result.
24. Lua exposes only the reviewed per-function allowlist.
25. MCP is absent from this implementation; any future mutating tool must forbid Tasks.
26. All diagnostics use `nr.utils:errorHandle`.
27. One active viewer owns the fixed `engine.ndjson` / `options.ndjson` paths; readers
    reopen and scan the active file after rotation, without process-pipe draining.
28. Fullscreen has no direct mutation bypass, model replacement has the one documented
    camera-derived commit, and actionable semantic node singleton preflight is enforced.
29. Architecture documents are synchronized when code migration changes implemented
    boundaries.

## 31. Deliberately Rejected Alternatives

- **Separate UI, WebSocket, and Lua registries:** inevitably drift and preserve multiple
  sources of truth.
- **Calling ImGui/node UI callbacks from WebSocket:** violates thread and callback-lifetime
  boundaries.
- **Keeping node drafts as canonical state:** allows UI and renderer snapshots to disagree.
- **Property/action public categories:** add parallel APIs without improving the chosen
  one-shot admission model.
- **Dedicated camera mutation methods:** bypass the unified option entry.
- **`set_many` or JSON-RPC batch mutation:** violates one-option and one-frame limits.
- **FIFO mutation queue:** hides backpressure and executes stale intent later.
- **Leasing/restoring a detached frame operation:** creates a hidden retry path; a
  frame-coupled no-submission outcome is terminal failure instead.
- **Last-write-wins overwrite/coalescing in `OptionSystem`:** can silently lose an explicit
  operation. Human camera combination happens before submission, not inside the mailbox.
- **Producer-specific `available` values:** would make the supposedly shared snapshot
  differ between UI, WebSocket, and Lua; authority remains session/adapter state.
- **MCP Tasks/renderer TaskStore:** introduces durable state, polling, TTL, cancellation,
  and result semantics the renderer does not need.
- **Operation-ID deduplication ledger:** adds retention and reconnect semantics solely to
  mask the accepted unknown-outcome case.
- **Automatic retry after timeout:** can duplicate a mutation whose success response was
  lost.
- **Returning capture bytes or transcoding EXR:** creates a large image transport and
  duplicates capabilities better owned by the agent environment.
- **Opening all Lua libraries and removing dangerous names:** blacklist gaps are too easy;
  V1 uses a constructed positive environment.
- **Creating logical option frames while minimized:** creates a second time model and can
  execute renderer operations without a renderable frame.
- **Hiding the snapshot in mutable `FrameServices`:** weakens the explicit frame-consistency
  boundary.

## 32. Implementation Readiness

The system design is approved and its implementation parameters are fixed in this
document: dependency pins, option catalog/ranges, token delivery, filesystem roots,
transport budgets, Lua budgets, and `NR_OPTION_V1` record format. Execution proceeds by
the phased vertical slices in §28; no additional architecture discussion is required.

A discovery that would require a mutation queue,
multiple mutations per frame, final-result RPC, task retention, or a second semantic
registry is an architectural change and must return to design review before coding
continues.

## 33. External References

- [JSON Lines format](https://jsonlines.org/)
- [PowerShell `Get-Content`, including `-Wait`](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.management/get-content)
- [JSON-RPC 2.0 Specification](https://www.jsonrpc.org/specification)
- [RFC 6455: The WebSocket Protocol](https://www.rfc-editor.org/rfc/rfc6455.html)
- [Boost.Beast](https://www.boost.org/library/latest/beast/)
- [Boost.JSON](https://www.boost.org/doc/libs/latest/libs/json/doc/html/index.html)
- [vcpkg 2026.06.24](https://github.com/microsoft/vcpkg/releases/tag/2026.06.24)
- [Lua version history](https://www.lua.org/versions.html)
- [Lua 5.5.0 bugs](https://www.lua.org/bugs.html#5.5)
- [Lua 5.5 Reference Manual](https://www.lua.org/manual/5.5/manual.html)
- [Lua `luaL_openselectedlibs`](https://www.lua.org/manual/5.5/manual.html#luaL_openselectedlibs)
- [MCP Tools specification, 2025-11-25](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)
- [MCP Tasks specification, 2025-11-25](https://modelcontextprotocol.io/specification/2025-11-25/basic/utilities/tasks)
