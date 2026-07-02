nr_add_contract_test(nr_renderpasses_as_build_contract_test nr_renderpasses_as_build_contract_test.cpp "fast;cpu;unit;renderpasses;rt"
    nrrenderpasses
    nrrenderer
    nrscene
    nrrhi
    utils
    dependency
)

nr_add_contract_test(nr_renderpasses_renderer_cache_contract_test nr_renderpasses_renderer_cache_contract_test.cpp "fast;cpu;unit;renderpasses;renderer-cache"
    nrrenderpasses
    nrrenderer
    utils
    dependency
)
