# Agent Interaction and Offline Automation Design

Status: accepted design direction; implementation has not started.

Decision date: 2026-07-24.

This document defines the planned interaction architecture for Newbie-Renderer:

- human interaction uses Dear ImGui plus WASD and mouse-look camera controls;
- live agent interaction uses a loopback WebSocket endpoint and semantic commands;
- offline automation uses an embedded Lua runtime;
- all three paths operate on one semantic control model, so an actionable UI control is
  added or removed once and the human, WebSocket, and Lua surfaces stay synchronized.

The repository currently treats Windows as a hard RHI target. This design does not change
that current fact. It keeps the interaction layer independent of Win32 so the same control,
WebSocket, and Lua implementation can be retained if Linux later becomes a supported RHI
target.

## 1. Problem Statement

The renderer needs two mutually exclusive interactive authority modes:

1. **Human mode**
   - Dear ImGui is directly editable.
   - WASD/QE movement and mouse rotation update the viewer camera.
2. **Agent mode**
   - An agent changes the same settings and invokes the same actions exposed by Dear
     ImGui, but addresses them by stable semantic identifiers instead of screen
     coordinates.
   - The agent sets the viewer camera position and orientation directly.
   - Dear ImGui remains visible as a state mirror, but it does not compete with the agent
     for write authority.

The renderer also needs an **offline automation backend**. A Lua script runs in-process,
uses the same semantic commands as the WebSocket client, advances in frame units, and does
not require a live network endpoint.

The synchronization requirement is stronger than merely exposing similar APIs. A stateful
or actionable control must have exactly one declaration and one validation/application
path. Dear ImGui, WebSocket, and Lua are presenters or command producers around that model;
none of them owns a parallel copy of renderer settings.

## 2. Goals

- Keep Dear ImGui behavior and existing human camera controls.
- Give agents stable discovery, read, write, action, and direct-camera operations.
- Make adding or removing one semantic control automatically affect every control surface.
- Preserve node-owned staged state and next-frame application where that is already the
  correct runtime contract.
- Apply all engine mutations on the renderer/application main thread.
- Use a versioned, bounded, authenticated loopback WebSocket protocol.
- Support an optional external MCP adapter without implementing MCP inside the renderer.
- Run Lua automation deterministically at frame boundaries.
- Keep platform-specific networking, Lua headers, and serialization dependencies behind
  narrow `dependency.*` modules.
- Keep the control core buildable and testable on Windows and Linux independently from the
  current Windows-only Vulkan presentation path.

## 3. Non-Goals

- Pixel-coordinate, screenshot-recognition, or synthetic-mouse control of Dear ImGui.
- Exposing raw Dear ImGui objects, Vulkan handles, render-graph objects, or node pointers to
  an agent or Lua.
- General remote-network administration. Version 1 is a same-machine loopback service.
- Internet-facing WebSocket hosting, TLS certificate management, or multi-user
  authorization.
- Replacing renderer/node validation with validation in the network or Lua layer.
- Running Dear ImGui or renderer state mutation from a networking thread.
- Making the current renderer headless. Offline Lua still uses the normal application and
  presentation lifetime unless a separate headless design is accepted later.
- Making Linux an immediate RHI target as part of this interaction work.

## 4. Current Integration Baseline

The implementation must evolve the current boundaries instead of building a second control
system beside them:

- [`UiSystem`](../src/app/nrAppUi.ixx) owns the Dear ImGui context and implements
  `nr::renderer::NodeUiWriter`.
- [`NodeUiWriter` and `NodeUiSection`](../src/renderer/nrRenderer.ixx) form the current
  renderer-to-app node UI sideband.
- [`ViewerControlState`](../src/pipeline/nrPipeline.cpp) contains app/viewer settings and a
  `ViewerPendingRequests` record used to commit changes at a later frame boundary.
- [`AppCamera`](../src/app/nrAppCamera.ixx) owns the viewer camera and samples presentation
  input in human mode.
- [`ViewerPerspectiveCamera`](../src/renderer/nrViewerCamera.ixx) already supports direct
  `setPose(...)`, `setPoseFromLookAt(...)`, and renderer camera override construction.
- The current viewer loop in [`nrPipeline.cpp`](../src/pipeline/nrPipeline.cpp) begins the
  UI frame, queues viewer sections, samples human camera input, builds a camera override,
  and renders.

The current immediate-mode API is insufficient as the agent contract because:

- display labels are not stable machine identifiers;
- `beginCombo`/`selectable` describe rendering flow rather than one semantic enum value;
- callbacks can capture node-owned mutable state and are only safe during their intended
  main-thread lifetime;
- a WebSocket thread cannot safely call a UI callback;
- an agent cannot reliably discover whether an item is read-only, an action, bounded, or
  conditionally available;
- independently reimplementing the same settings in a server would inevitably drift from
  Dear ImGui.

## 5. Architectural Decision

Introduce a transport-neutral semantic interaction layer. Dear ImGui becomes a presenter
for it, WebSocket becomes a live command transport, and Lua becomes an offline command
producer.

```text
                                     optional
 Generic agent <---- MCP ----> agent adapter/sidecar
                                      |
 Direct agent ------------------------+
                                      |
                         loopback WebSocket + JSON
                                      |
                             WebSocket I/O thread
                                      |
                               bounded queues
                                      |
                                      v
 Human input ----------> main-thread InteractionController <---------- Lua coroutine
                              |                    |
                              |                    +--> camera handler
                              |
                              +--> ControlRegistry + CommandDispatcher
                                           |
                       +-------------------+-------------------+
                       |                                       |
              domain-owned staged state                 catalog snapshot
                       |                                       |
             renderer/app/node behavior                 ImGui presenter
```

The important boundary is `InteractionController`. Network and Lua code create typed
commands. Only the main-thread controller resolves those commands against live bindings and
mutates application, camera, renderer, or node-owned staged state.

### 5.1 Proposed module responsibilities

The exact partition names may change during implementation, but dependencies should follow
this shape:

| Proposed module | Responsibility | Must not depend on |
| --- | --- | --- |
| `nr.interaction:model` | IDs, descriptors, values, commands, results, revisions | ImGui, WebSocket, Lua, Vulkan |
| `nr.interaction:registry` | Live catalog construction, lookup, generation invalidation | ImGui, WebSocket, Lua |
| `nr.interaction:dispatcher` | Main-thread validation, ordering, application, result routing | Transport-specific APIs |
| `nr.app:interaction` | Authority mode, frame safe point, app and camera handlers | Raw third-party headers |
| `nr.app:interactionUi` | Dear ImGui presentation of semantic controls | Network and Lua APIs |
| `nr.app:webSocketControl` | Server lifetime, session policy, queue bridge | Renderer internals |
| `nr.app:luaAutomation` | Sandboxed Lua state and frame coroutine scheduler | Raw renderer/Vulkan objects |
| `dependency.network` | Narrow Boost.Asio/Beast and JSON boundary | Project business logic |
| `dependency.lua` | Narrow embedded Lua C API boundary | Project business logic |

`nr.renderer` may consume the transport-neutral descriptor/writer types needed for node
controls, but it must not know that WebSocket or Lua exists. The network server and Lua
runtime belong above renderer orchestration in `nr.app`.

If `nr.interaction` becomes a new stable module during implementation, the global
architecture context must be updated in the same patch set. This design document alone does
not describe it as current architecture.

## 6. Single Semantic Control Model

### 6.1 Synchronization invariant

After migration, the following rule is mandatory:

> Every stateful or actionable Dear ImGui item is declared as a semantic control. Dear
> ImGui renders that declaration; WebSocket and Lua discover and operate on the same
> declaration.

Consequences:

- Adding one semantic declaration adds the control to Dear ImGui, the WebSocket catalog,
  and Lua discovery.
- Removing the declaration removes it from all three surfaces.
- Renaming a display label does not break automation because the stable ID is unchanged.
- Changing range, enum, access, availability, or validation metadata changes every surface
  together.
- Decorative spacing, separators, and non-semantic explanatory text do not need remote
  commands. Read-only status values should be declared as telemetry when agents need to
  observe them.
- Direct `ImGui::*` calls for new stateful controls are not allowed outside the semantic
  presenter after migration.

### 6.2 Stable identifiers

Every control uses an explicit ASCII identifier. IDs are not derived from display labels or
section order.

Recommended form:

```text
<scope>.<subsystem>.<control>
```

Examples:

```text
viewer.pipeline.active
viewer.environment.active
viewer.model.load
render.path_tracing.max_surface_bounces
render.present.tone_mapping
render.present.capture
render.dlss_rr.quality
```

Rules:

- lower-case dot-separated identifiers;
- no runtime pointer, localized text, array index, or ImGui `##` suffix in an ID;
- IDs remain stable across label and layout changes;
- one active catalog cannot contain duplicate IDs;
- a graph- or node-specific control is available only while its owning runtime is active;
- reusing an old ID for different semantics is a protocol-breaking change.

Sections also have stable IDs. A control ID is globally unique; its `section_id` is
presentation and discovery metadata, not part of lookup behavior.

### 6.3 Control kinds and values

The version 1 value model should cover the existing UI surface without exposing ImGui
implementation details:

```text
ControlValue =
    bool
  | int64
  | uint64
  | double
  | string
  | enum token
  | vec2
  | vec3
```

Version 1 control kinds:

| Kind | Access pattern | Required schema |
| --- | --- | --- |
| `boolean` | read/write or read-only | default/current value |
| `integer` | read/write or read-only | minimum, maximum, optional step |
| `unsigned_integer` | read/write or read-only | minimum, maximum, optional step |
| `number` | read/write or read-only | minimum, maximum, optional step, unit |
| `string` | read/write or read-only | maximum length and optional format |
| `enum` | read/write or read-only | stable token/display-name pairs |
| `action` | invoke only | optional argument schema |
| `telemetry` | read-only | value type and unit |

Enum wire values are stable tokens, not display labels or C++ ordinal values. For example,
`"bt2390"` remains valid if the UI label later becomes `"BT.2390 EETF"`.

Camera pose is a dedicated command domain rather than an ordinary group of scalar controls.
That keeps position and orientation updates atomic.

### 6.4 Descriptor shape

A descriptor needs enough information for rendering, discovery, validation, and conflict
handling:

```text
ControlDescriptor
  id
  section_id
  display_name
  description
  kind
  access
  value_schema
  presentation_hint
  availability
  catalog_revision
  value_revision
```

`presentation_hint` may request a slider, input field, combo, checkbox, or button, but it is
not part of command semantics. Agents operate on `kind`, `access`, and `value_schema`.

`availability` distinguishes:

- `active`: readable and, if allowed by authority, mutable;
- `read_only`: currently observable but not mutable;
- `disabled`: declared but temporarily cannot run;
- `hidden`: omitted from Dear ImGui but may remain discoverable when explicitly useful.

Controls that only exist for the active render graph are removed when that graph is
uninstalled. Controls that are merely conditionally usable should normally remain in the
catalog with `disabled` status so agents can understand why an operation is unavailable.

### 6.5 Bindings and ownership

Descriptors must not expose raw owning pointers. A live binding consists conceptually of:

- a read callback that produces the current `ControlValue`;
- a command builder or setter callback that validates domain rules and stages a change;
- optional availability and description callbacks;
- an owner generation associated with the app, pipeline, graph, or node lifetime.

The catalog may be rebuilt from semantic section declarations each frame. The published
snapshot is immutable and contains values, metadata, and revisions but no callable
function, reference, or pointer.

Graph replacement must invalidate the old owner generation before the old node runtimes are
destroyed. Commands referencing the old catalog or owner generation fail as stale instead
of invoking a callback captured from an obsolete graph.

### 6.6 Migrating the immediate-mode writer

The migration should replace rendering-oriented compound interactions with semantic
operations:

| Current pattern | Semantic replacement |
| --- | --- |
| `checkbox(label, bool&)` | `boolean(id, label, binding)` |
| `sliderFloat(label, value, min, max)` | `number(id, label, binding, schema, slider_hint)` |
| `inputUInt(...)` | `unsignedInteger(id, label, binding, schema)` |
| `beginCombo` + `selectable` | `enumeration(id, label, binding, token list)` |
| `button(label)` | `action(id, label, handler)` |
| `text(...)` with observable state | `telemetry(id, label, reader)` |
| decorative `text(...)` | presenter-only text |

Existing app and node UI can be migrated section by section. During migration, compatibility
overloads may still render controls, but they must not be considered agent-addressable. The
WebSocket endpoint should not be declared feature-complete until all intended stateful
controls have explicit IDs.

## 7. Authority Modes

Use one explicit process-level authority mode:

```text
InteractionMode::human
InteractionMode::agent
InteractionMode::offlineLua
```

Mode is selected at startup for version 1. Dynamic authority transfer is deferred because it
introduces partially applied input, cursor capture, and connection-lease edge cases.

### 7.1 Human mode

- The WebSocket control server is not started.
- Dear ImGui semantic controls are writable.
- `AppCamera::updateFromPresentation(...)` samples WASD/QE and mouse rotation.
- UI and camera changes enter the same main-thread command/application path used by the
  other modes.

### 7.2 Agent mode

- The loopback WebSocket server is started.
- One authenticated connection owns the controller lease.
- Dear ImGui remains visible and reflects the current catalog values.
- Dear ImGui stateful controls remain read-only throughout agent mode, including between
  controller leases.
- WASD/QE and mouse-look mutation are skipped.
- Window event polling, close handling, resize behavior, and rendering continue normally.
- The agent uses semantic control commands and dedicated camera commands.
- Additional connections may be rejected or admitted as read-only observers; they cannot
  acquire write authority in version 1.

On controller disconnect, the renderer keeps the last committed state and waits for a new
authenticated controller. It does not silently switch to human authority.

### 7.3 Offline Lua mode

- The WebSocket server is not started.
- Dear ImGui may remain visible as a read-only state mirror.
- Human UI and camera mutation are disabled.
- The Lua scheduler is the sole command producer.
- The process exits with a non-zero result when the script fails, times out, or requests an
  invalid operation under strict automation policy.

## 8. Main-Thread Command and Frame Model

### 8.1 Thread ownership

The WebSocket implementation uses a dedicated asynchronous I/O thread. That thread may:

- accept, authenticate, read, parse, and structurally bound messages;
- enqueue transport-neutral requests;
- dequeue already-produced responses and events;
- maintain ping, pong, idle timeout, and connection lifetime.

It must not:

- call Dear ImGui;
- access `AppCamera`, renderer, scene, graph, node, or pipeline mutable state;
- run a semantic read callback;
- validate a value against live domain state;
- install or uninstall a render graph.

Lua runs on the main thread at an automation safe point. It still emits semantic commands
rather than receiving direct engine object access.

### 8.2 Queue model

Use bounded queues in both directions:

```text
WebSocket I/O -> inbound request queue -> main thread
main thread -> outbound result/event queue -> WebSocket I/O
```

Requirements:

- FIFO ordering for requests from the controlling session;
- an explicit maximum request count and byte budget;
- a `queue_full` result rather than unbounded allocation;
- no success response until main-thread semantic validation and application complete;
- transport parse/authentication failures may be returned immediately because they do not
  inspect engine state;
- responses retain the originating session and request ID.

The concrete queue may use a mutex and condition variable initially. Lock-free structures
are unnecessary for low-rate UI and camera commands unless profiling proves otherwise.

### 8.3 Frame safe point

The application loop should gain one explicit interaction safe point before pending viewer
requests are committed:

1. Poll or preserve normal window lifecycle behavior.
2. Resume the Lua coroutine or drain WebSocket requests, according to authority mode.
3. Resolve commands against the last published catalog and current owner generations.
4. Validate every value and stage accepted domain changes.
5. Commit viewer/pipeline/node pending requests at their existing defined boundary.
6. In human mode only, sample presentation camera input.
7. Build the current semantic catalog and render its Dear ImGui presentation.
8. Build the camera override and render the frame.
9. Publish catalog/value/camera revisions and command results.

The precise insertion point may preserve the current `processPendingRequests(...)` ordering,
but both human and agent changes must have the same staging and commit semantics. A
WebSocket request received halfway through a frame is not applied concurrently; it waits
for the next safe point.

### 8.4 Command ordering and transactions

- A controller's commands are applied in received order.
- A camera pose update is atomic.
- `control.set_many` validates every target, type, range, availability, and expected
  revision before changing any value. It then stages all values in one frame transaction.
- Actions are excluded from `set_many` because arbitrary actions are not generally
  reversible.
- An action is invoked with a separate `action.invoke` request.
- Commands may include `if_catalog_revision` and `if_value_revision` preconditions.
- A stale precondition fails without mutation.
- Repeating the same idempotent value assignment is allowed and reports whether the
  effective state changed.
- Action requests are not assumed idempotent; clients must use unique request IDs and avoid
  blind retries after an unknown connection failure.

### 8.5 Revisions

Maintain three independent monotonic revisions:

- `catalog_revision`: descriptor membership or metadata changed;
- `value_revision`: one or more semantic control values changed;
- `camera_revision`: the viewer camera pose or lens changed.

Revisions let clients cache state and detect races without sending a full catalog every
frame. A catalog rebuild that produces identical descriptors does not increment
`catalog_revision`.

## 9. WebSocket Transport

WebSocket is a suitable live transport because it provides one persistent, ordered,
bidirectional connection over TCP. The protocol is standardized by
[RFC 6455](https://www.rfc-editor.org/info/rfc6455/).

### 9.1 Implementation dependency

The recommended initial C++ implementation is Boost.Asio + Boost.Beast:

- Beast provides portable HTTP and WebSocket operations using Asio's asynchronous model;
- it supports Windows and Linux;
- it provides asynchronous server examples and timeout support;
- error-code overloads can be used so project code does not depend on exceptions for normal
  network failure.

See the official [Boost.Beast library page](https://www.boost.org/library/latest/beast/)
and [Beast documentation](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/index.html).

This choice should be confirmed with a small dependency/build-time spike before
implementation. Regardless of the concrete library, all Boost/Asio/Beast and JSON headers
remain under `src/extern` and are re-exported through narrow `dependency.network` and
serialization adapters. Core interaction modules receive project-owned request/result
types only.

### 9.2 Endpoint policy

Version 1 endpoint requirements:

- bind only to explicit loopback addresses (`127.0.0.1` and, when configured, `::1`);
- never bind to `0.0.0.0` or `::` by default;
- use port `0` by default and report the selected port through a machine-readable startup
  record;
- use path `/nr/control`;
- require WebSocket subprotocol `nr-control-v1`;
- accept UTF-8 JSON text messages only;
- disable WebSocket compression initially;
- set a maximum message size, recommended default 1 MiB;
- enforce handshake, idle, and write timeouts;
- use ping/pong health checks;
- close connections that repeatedly violate schema or rate limits.

TLS is unnecessary for the loopback-only version 1 threat model. Any future non-loopback
mode requires a separate security design and `wss://`.

### 9.3 Authentication and browser-origin protection

Agent mode requires a cryptographically random bearer token:

- the launcher or sidecar supplies it through `NR_CONTROL_TOKEN`;
- the renderer fails fast if agent mode starts without a token;
- the client sends it in the HTTP Upgrade `Authorization` header;
- the token is never placed in the URL or normal logs;
- comparison is constant-time;
- a token is valid only for the current process session.

Native clients normally omit `Origin`. If an `Origin` header is present, the server accepts
only an explicit allowlist and otherwise rejects the handshake. Localhost binding,
authentication, and Origin validation follow the same DNS-rebinding protections required
for local MCP HTTP servers in the
[MCP transport specification](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports).

### 9.4 Session and controller lease

After the WebSocket handshake:

1. The client sends `session.hello`.
2. The renderer verifies application protocol version and requested role.
3. The renderer returns `session.welcome` with a random session ID, authority mode, active
   role, catalog revision, value revision, and camera revision.
4. Exactly one session may hold the controller lease.
5. The lease ends when that socket closes or the renderer shuts down.

Version 1 does not support lease stealing. A second controller request receives
`controller_unavailable`.

## 10. WebSocket Application Protocol

The WebSocket subprotocol is engine-owned, transport-neutral JSON. It is not MCP and it does
not expose Dear ImGui operations.

### 10.1 Request envelope

```json
{
  "protocol": "nr.control",
  "version": 1,
  "request_id": "req-42",
  "method": "control.set",
  "params": {
    "id": "render.path_tracing.max_surface_bounces",
    "value": 8,
    "if_catalog_revision": 12
  }
}
```

`request_id` is an opaque client-generated UTF-8 string with a bounded length. It is unique
within one session.

### 10.2 Success response

```json
{
  "protocol": "nr.control",
  "version": 1,
  "request_id": "req-42",
  "result": {
    "applied": true,
    "effective_value": 8,
    "catalog_revision": 12,
    "value_revision": 91,
    "applied_frame": 384
  }
}
```

### 10.3 Error response

```json
{
  "protocol": "nr.control",
  "version": 1,
  "request_id": "req-42",
  "error": {
    "code": "out_of_range",
    "message": "Value must be between 1 and 64.",
    "details": {
      "id": "render.path_tracing.max_surface_bounces",
      "minimum": 1,
      "maximum": 64
    }
  }
}
```

Errors contain safe user-facing context, not credentials, stack traces, source addresses,
or internal object identities.

### 10.4 Server event envelope

```json
{
  "protocol": "nr.control",
  "version": 1,
  "event": "catalog.changed",
  "params": {
    "catalog_revision": 13
  }
}
```

Clients subscribe explicitly to value and camera events. Catalog invalidation is always
delivered to the controller because continuing with a stale catalog is unsafe.

### 10.5 Version 1 methods

| Method | Purpose | Write authority required |
| --- | --- | --- |
| `session.hello` | Negotiate protocol and role | No |
| `session.describe` | Read mode, revisions, frame, and capabilities | No |
| `catalog.get` | Fetch the immutable semantic catalog snapshot | No |
| `control.get` | Read one control and its revision | No |
| `control.set` | Stage one writable value | Yes |
| `control.set_many` | Atomically validate and stage several writable values | Yes |
| `action.invoke` | Invoke one semantic action | Yes |
| `camera.get` | Read pose, lens, and camera revision | No |
| `camera.set_pose` | Atomically set position, yaw, and pitch | Yes |
| `camera.look_at` | Atomically set position and look-at target | Yes |
| `events.subscribe` | Select value/camera/telemetry event classes | No |

Direct methods such as `imgui.click`, `imgui.set_cursor`, or `imgui.find_label` are
intentionally absent.

### 10.6 Catalog example

```json
{
  "catalog_revision": 12,
  "controls": [
    {
      "id": "render.path_tracing.max_surface_bounces",
      "section_id": "render.path_tracing",
      "display_name": "Max Surface Bounces",
      "description": "Maximum path depth before termination policies apply.",
      "kind": "unsigned_integer",
      "access": "read_write",
      "availability": "active",
      "value": 6,
      "value_revision": 90,
      "schema": {
        "minimum": 1,
        "maximum": 64,
        "step": 1
      },
      "presentation_hint": "input"
    }
  ]
}
```

### 10.7 Error codes

Version 1 should define at least:

```text
malformed_message
unsupported_protocol
unsupported_version
unauthorized
invalid_mode
controller_unavailable
duplicate_request_id
catalog_not_ready
stale_catalog
stale_value
control_not_found
control_unavailable
read_only
type_mismatch
out_of_range
invalid_argument
queue_full
timeout
internal_error
```

Domain failures are reported through `nr.utils:errorHandle` and converted to the narrow
protocol result at the app boundary. The transport must not create a second project-local
diagnostic facility.

## 11. Camera Contract

Camera commands operate on [`ViewerPerspectiveCamera`](../src/renderer/nrViewerCamera.ixx)
through `AppCamera`, not on matrices supplied by the client.

### 11.1 Canonical pose

Version 1 matches the current camera model:

```json
{
  "position": [1.0, 2.0, 3.0],
  "yaw_degrees": -90.0,
  "pitch_degrees": -10.0
}
```

Rules:

- position uses renderer world units;
- wire angles use degrees and are named with the `_degrees` suffix;
- the app converts to radians at the existing camera boundary;
- all values must be finite;
- pitch is validated against the configured camera pitch limit;
- yaw may be normalized without changing the represented orientation;
- roll is unsupported in protocol version 1 because the current viewer camera does not
  model roll;
- the update is exact and atomic, with no implicit smoothing.

### 11.2 Look-at convenience command

```json
{
  "position": [1.0, 2.0, 3.0],
  "target": [0.0, 0.0, 0.0],
  "world_up": [0.0, 1.0, 0.0]
}
```

The app validates finite vectors, rejects a zero-length view direction, and calls the
existing `setPoseFromLookAt(...)` path.

### 11.3 Human and agent parity

Human movement and agent pose changes must eventually publish the same camera-change signal
and `camera_revision`. Temporal reset or history-retention policy remains owned by the
renderer/temporal feature that consumes the camera change; it must not vary merely because
the input originated from WebSocket instead of WASD.

## 12. Optional MCP Adapter

MCP remains outside the renderer:

```text
MCP client <-> MCP adapter/sidecar <-> nr-control-v1 WebSocket <-> renderer
```

Reasons:

- the renderer retains one small engine-owned protocol;
- direct agents can use WebSocket without MCP;
- MCP SDK and protocol updates do not change renderer internals;
- the adapter can use an officially supported MCP SDK;
- MCP's standard transports are stdio and Streamable HTTP, while WebSocket remains the
  engine-side live transport. See the
  [MCP transport specification](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports).

The adapter should expose a fixed, small tool set rather than one generated MCP tool per UI
control:

```text
list_controls
get_control
set_control
set_controls
invoke_action
get_camera
set_camera_pose
look_at
```

Dynamic UI additions and removals then appear through `list_controls` and catalog revision
changes without changing the MCP tool schema.

The adapter is optional deployment infrastructure. WebSocket protocol correctness and Lua
automation must not depend on it.

## 13. Lua Offline Automation

### 13.1 Runtime choice and dependency boundary

Use an embedded Lua 5.5-series runtime and pin the exact source/package version in the
project dependency lock. Lua has a small C embedding API and built-in cooperative
coroutines. Lua versions are not ABI-compatible, so scripts should be distributed as source
rather than precompiled bytecode. See the official
[Lua version history](https://www.lua.org/versions.html) and
[Lua 5.5 reference manual](https://www.lua.org/manual/5.5/manual.html).

The raw Lua headers and state live behind `dependency.lua`. Project code should own an RAII
wrapper for `lua_State`, use protected calls/resume status codes at the immediate external
library boundary, and report failures through `nr.utils:errorHandle`.

The initial API surface is small enough that a project-owned narrow C API adapter is
preferred over exposing a general C++ binding framework throughout app code.

### 13.2 Execution model

Lua automation runs as one coroutine:

1. Initialize app, renderer, selected pipeline, and optional scene normally.
2. Build and publish the initial semantic catalog.
3. Load the script from the configured automation root.
4. Resume the coroutine at the main-thread interaction safe point.
5. A frame-wait API yields the coroutine.
6. Render the requested number of frames.
7. Resume the coroutine after the wait condition is satisfied.
8. Exit successfully when the coroutine returns.

This uses Lua's cooperative coroutine model and C API `lua_resume`/yield support described
in the [Lua manual](https://www.lua.org/manual/5.5/manual.html#2.6). The script must never
busy-wait on the render thread.

### 13.3 Lua API

Expose one read-only global module named `nr`:

```text
nr.session.describe()
nr.controls.list()
nr.controls.get(id)
nr.controls.set(id, value, options?)
nr.controls.set_many(values, options?)
nr.actions.invoke(id, args?)
nr.camera.get()
nr.camera.set_pose(pose)
nr.camera.look_at(position, target, world_up?)
nr.frame.index()
nr.frame.wait(frame_count)
nr.frame.wait_until(predicate, timeout_frames)
nr.log.info(message)
nr.log.warning(message)
```

Lua functions translate inputs to the same typed commands used by WebSocket. They do not
hold references to descriptor callbacks or engine objects across yields.

An example script:

```lua
local session, session_error = nr.session.describe()
assert(session, session_error)

local ok, camera_error = nr.camera.look_at(
    { 2.0, 1.5, 4.0 },
    { 0.0, 0.8, 0.0 },
    { 0.0, 1.0, 0.0 })
assert(ok, camera_error)

local controls_ok, controls_error = nr.controls.set_many({
    ["render.path_tracing.max_surface_bounces"] = 8,
    ["render.present.tone_mapping"] = "aces"
})
assert(controls_ok, controls_error)

nr.frame.wait(8)

local capture_ok, capture_error =
    nr.actions.invoke("render.present.capture")
assert(capture_ok, capture_error)

nr.frame.wait(3)
```

The example assumes those controls are active in the selected graph. If a control is absent,
disabled, stale, or out of range, Lua receives the same semantic error code as a WebSocket
client.

### 13.4 Determinism

Offline automation should prefer frame-based behavior:

- waits use frame counts, not wall-clock sleeps;
- an optional fixed `deltaSeconds` is configured for the run;
- scripts start only after the initial catalog is ready;
- commands are applied at the same safe point in every run;
- unordered table iteration is not used to define command order;
- `set_many` sorts by stable control ID internally after validating the transaction;
- any exposed random source uses an explicit script seed;
- output paths are normalized and restricted to the configured automation output root;
- the process returns a non-zero exit code for script errors or timeouts.

Deterministic CPU command sequencing does not promise bit-identical GPU output across driver
versions. Image comparison policy belongs to a later rendering-regression design.

### 13.5 Sandbox and resource limits

Do not expose a general-purpose local execution environment by default:

- open only the base, coroutine, table, string, math, and UTF-8 libraries;
- do not expose `io`, `os`, `debug`, `package.loadlib`, or arbitrary native modules;
- do not expose FFI;
- restrict script loading to a configured automation root;
- reject path traversal outside that root;
- use a custom Lua allocator to enforce a memory budget;
- use an internal instruction-count hook and per-resume time budget;
- limit log message size and total emitted log volume;
- terminate the automation run cleanly when a budget is exceeded.

The internal hook may use Lua debug facilities, but the `debug` library itself is not
available to the script.

## 14. Cross-Platform Requirements

The interaction design must not reproduce the current Windows RHI assumption in higher
layers:

- `nr.interaction` uses only the C++ standard library and project-owned types.
- WebSocket code uses the portable networking dependency wrapper, not Winsock calls.
- Lua code uses the same embedded C API on Windows and Linux.
- No platform branch is allowed in control IDs, JSON schema, command semantics, or Lua API.
- Paths use `std::filesystem` and are normalized against explicit roots.
- UTF-8 is the protocol and script-facing text encoding.
- Loopback address selection is configuration, not `#ifdef` behavior.
- Platform-specific service startup or file-permission work, if later required, stays in a
  narrow platform adapter.

The first Linux validation target can build and test:

- semantic model and registry;
- command dispatcher;
- JSON codec;
- WebSocket server/client loopback tests;
- Lua scheduler and bindings.

That validation does not require the Vulkan presentation application to run on Linux. Full
Linux renderer support requires a separate accepted RHI/presentation plan and corresponding
updates to `AGENTS.md` and the architecture context.

## 15. Dependency and Error-Handling Policy

The implementation must follow existing project policy:

- add Boost networking/JSON and Lua through `src/extern` plus narrow `dependency.*`
  modules;
- do not include raw Boost or Lua headers in core interaction, renderer, render-pass, or
  pipeline logic;
- use RAII for server, connection, queue, and Lua-state lifetime;
- use Boost error-code APIs for expected network errors;
- contain Lua protected-call/resume handling at the immediate external boundary;
- report project-visible failures through `nr.utils:errorHandle`;
- use `std::map` for small control/catalog maps unless profiling demonstrates a typical
  size above the project's unordered-container threshold;
- split non-trivial module partitions into `.ixx` interfaces and `.cpp` implementation
  units.

## 16. Security and Robustness Checklist

- Loopback-only bind by default.
- Mandatory bearer token in agent mode.
- Origin allowlist or rejection.
- One controller lease.
- Bounded message size.
- Bounded inbound and outbound queues.
- Bounded control ID, request ID, string value, and error message lengths.
- Strict JSON schema and rejection of unknown required-version fields.
- No NaN or infinity in numeric values.
- Per-control domain validation on the main thread.
- No raw pointers, addresses, handles, or stack traces in protocol results.
- Ping/pong and idle timeouts.
- Clean close on renderer shutdown.
- Invalidate graph-owned bindings before node destruction.
- Reject stale catalog/value revisions without mutation.
- Lua path, memory, instruction, and execution-time limits.
- No automatic fallback from agent authority to human authority after disconnect.

## 17. Verification Strategy

### 17.1 Unit tests

- ID syntax and duplicate rejection.
- Descriptor schema validation for every control kind.
- Enum token stability and invalid-token rejection.
- Range, finite-number, string-length, and access validation.
- Catalog revision changes only when membership or metadata changes.
- Value revision changes only when effective values change.
- Owner-generation invalidation rejects stale commands.
- FIFO command order and `set_many` all-or-nothing validation.
- Camera pose/look-at validation, degree conversion, and atomic updates.
- JSON request/response/event round trips.
- Protocol version and unknown-method handling.
- Queue capacity behavior.
- Lua value conversion, protected errors, yield/resume, timeout, and sandbox policy.

### 17.2 Integration tests

- Authenticated WebSocket handshake succeeds on loopback.
- Missing/invalid token, invalid Origin, wrong subprotocol, and oversized messages fail.
- A second controller cannot acquire the lease.
- `catalog.get`, `control.get`, `control.set`, action invocation, and camera commands execute
  on the main thread and return the applied frame.
- Adding one test semantic control makes it visible through Dear ImGui's presenter model,
  WebSocket catalog, and Lua discovery.
- Removing that declaration removes it from all three surfaces and causes an old command to
  fail as stale/not found.
- The same value change through human UI, WebSocket, and Lua produces the same staged and
  effective domain state.
- Graph replacement invalidates old node controls before node destruction.
- Controller disconnect preserves state and permits a later authenticated controller.
- One Lua script produces the same command/frame trace on repeated runs.

### 17.3 Cross-platform CI

When Linux CI becomes available, run interaction/library unit tests and loopback integration
tests on both Windows and Linux. Do not make those tests depend on a Linux Vulkan
presentation implementation.

## 18. Incremental Implementation Plan

### Phase 1: semantic foundation

- Add transport-neutral control IDs, values, descriptors, results, revisions, and catalog.
- Add main-thread dispatcher and owner-generation invalidation.
- Add unit tests without WebSocket, Lua, or ImGui dependencies.

### Phase 2: Dear ImGui migration

- Add semantic section/control declaration APIs.
- Make `UiSystem` a presenter for those declarations.
- Convert app viewer controls.
- Convert node controls, including combos and one-shot actions.
- Classify decorative text versus agent-readable telemetry.
- Add a contract test that every intended stateful UI control has an explicit stable ID.

At the end of this phase, human behavior should remain equivalent, but there is still no
remote endpoint.

### Phase 3: authority and camera commands

- Add startup-selected interaction mode.
- Route human UI changes through the dispatcher.
- Add agent/offline direct camera pose and look-at commands.
- Add revisions, applied-frame results, and the frame safe point.
- Make Dear ImGui read-only in non-human authority modes.

### Phase 4: WebSocket transport

- Add the external networking/JSON dependency boundary.
- Implement loopback server RAII lifetime, authentication, lease, bounded queues, and
  protocol codec.
- Implement discovery, control, action, camera, and event methods.
- Add security and integration tests.

### Phase 5: Lua offline backend

- Add the Lua dependency boundary and RAII state wrapper.
- Add the sandboxed `nr` API.
- Add coroutine frame scheduling, fixed-delta option, budgets, and exit behavior.
- Add deterministic script integration tests.

### Phase 6: optional MCP adapter

- Build a separate adapter using an officially supported MCP SDK.
- Keep the MCP tool set fixed and discover semantic controls through the WebSocket catalog.
- Do not add MCP SDK dependencies to Newbie-Renderer.

### Phase 7: Linux portability gate

- Build and test interaction, WebSocket, JSON, and Lua layers on Linux.
- Remove any accidental platform assumption in those layers.
- Treat full Linux Vulkan/window support as a separate architecture change.

## 19. Acceptance Criteria

The design is implemented successfully when all of the following are true:

1. Human mode preserves editable Dear ImGui and WASD/QE plus mouse-look behavior.
2. Agent mode exposes no pixel- or label-coordinate UI automation.
3. One stable semantic declaration drives Dear ImGui, WebSocket discovery, and Lua.
4. Adding/removing a semantic control synchronizes all three surfaces without a second
   manual registration list.
5. One WebSocket controller can read/set controls, invoke actions, and set/get camera pose.
6. Dear ImGui visibly mirrors agent and Lua changes.
7. No network thread accesses ImGui or mutable renderer/app/node state.
8. Removed graph/node controls cannot be invoked through stale callbacks.
9. Lua uses the same dispatcher and validation as WebSocket.
10. Offline waits and command application are frame-based and bounded.
11. The server is loopback-only, authenticated, Origin-checked, and resource-bounded.
12. Interaction/WebSocket/Lua tests can run on Windows and Linux independently of Linux RHI
    availability.

## 20. Rejected Alternatives

- **Synthetic input or image-based ImGui automation:** brittle under layout, DPI, label, and
  frame-timing changes; cannot provide reliable typed validation.
- **A second manually maintained agent settings API:** violates the synchronization
  requirement and will drift from Dear ImGui.
- **Windows Named Pipe as the primary transport:** local and viable, but it introduces a
  different Linux transport and test path.
- **Shared memory for UI commands:** unnecessary synchronization and lifecycle complexity
  for low-rate messages.
- **gRPC for version 1:** strong typing is useful, but protobuf/code generation and service
  weight are disproportionate to the current control surface.
- **MCP implemented directly inside the renderer:** couples engine code to an agent protocol
  and SDK lifecycle; an external adapter gives the same agent compatibility.
- **Embedded Python or .NET as the primary live agent runtime:** larger runtime, packaging,
  failure-isolation, and lifecycle cost than a small WebSocket server.
- **Lua access to raw engine objects:** unsafe across graph/frame lifetimes and impossible to
  keep transport-equivalent.

## 21. Decisions Still Required Before Coding

The architectural direction is fixed, but implementation should explicitly close these
items:

- confirm Boost.Beast plus the chosen JSON library through a dependency/build-time spike;
- pin the exact Lua 5.5 patch version in the dependency lock;
- choose default queue counts, byte budgets, idle timeout, Lua memory budget, instruction
  budget, and per-resume time budget;
- define the exact machine-readable startup endpoint record consumed by a launcher or MCP
  adapter;
- enumerate the initial control IDs and decide which current status text becomes telemetry;
- decide whether screenshot/capture remains only an existing semantic action or receives a
  richer automation artifact API in a separate design.

These choices refine the implementation but do not change the selected WebSocket live
transport, Lua offline backend, single semantic control model, or main-thread authority
boundary.
