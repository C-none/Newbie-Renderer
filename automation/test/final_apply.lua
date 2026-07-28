local snapshot = nr.options.snapshot()
local started, reason = nr.options.apply(
    "viewer.window.fullscreen",
    true,
    { snapshot_token = snapshot.snapshot_token }
)
assert(started == true)
assert(reason == nil)
