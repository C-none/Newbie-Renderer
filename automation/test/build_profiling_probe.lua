-- [TEMP-BUILD-PROFILING] Temporary automation probe for RDG build-stage timing. Remove with the profiling patch.
local function apply(id, value)
    local snapshot = nr.options.snapshot()
    local started, reason = nr.options.apply(id, value, { snapshot_token = snapshot.snapshot_token })
    assert(started == true, tostring(reason))
    nr.frame.next()
end

apply("viewer.pipeline.selected", "rtobject")
apply("debug.build_profiling.report_interval_frames", 100)
apply("debug.build_profiling.enabled", true)

for _ = 1, 400 do
    nr.frame.next()
end
