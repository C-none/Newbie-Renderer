local snapshot = nr.options.snapshot()
assert(snapshot.schema_version == 1)
assert(snapshot.frame_index == 0)
assert(type(snapshot.snapshot_token) == "string")

local current = nr.options.get("viewer.window.fullscreen")
assert(current.option.value == false)

local started, reason = nr.options.apply(
    "viewer.window.fullscreen",
    true,
    { snapshot_token = snapshot.snapshot_token }
)
assert(started == true)
assert(reason == nil)

local second_started, second_reason = nr.options.apply(
    "viewer.camera.vertical_fov_degrees",
    75,
    { binding_epoch = snapshot.binding_epoch }
)
assert(second_started == false)
assert(second_reason == "operation_busy")

nr.frame.next()

local updated = nr.options.get("viewer.window.fullscreen")
assert(updated.option.value == true)
