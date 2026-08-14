nr_add_contract_test(nr_renderpasses_as_build_contract_test nr_renderpasses_as_build_contract_test.cpp "fast;cpu;unit;renderpasses;rt"
    nrrenderpasses
    nrrenderer
    nrscene
    nrrhi
    utils
)

nr_add_contract_test(nr_renderpasses_renderer_cache_contract_test nr_renderpasses_renderer_cache_contract_test.cpp "fast;cpu;unit;renderpasses;renderer-cache"
    nr_test_options
    nrrenderpasses
    nrrenderer
    nrscene
    nrresource
    utils
    dependency_services
)

nr_add_contract_test(nr_renderpasses_dlss_rr_contract_test nr_renderpasses_dlss_rr_contract_test.cpp "fast;cpu;unit;renderpasses;dlss;rr"
    nrrenderpasses
    nrrenderer
    nrrhi
    utils
    dependency_math
    dependency_graphics
)

nr_add_contract_test(nr_renderpasses_neural_checkpoint_contract_test nr_renderpasses_neural_checkpoint_contract_test.cpp "fast;cpu;unit;renderpasses;neural;checkpoint"
    nrrenderpasses
    nr_test_options
    utils
)

nr_add_contract_test(nr_renderpasses_neural_quality_contract_test nr_renderpasses_neural_quality_contract_test.cpp "fast;cpu;unit;renderpasses;neural;quality"
    nrrenderpasses
)
