nr_add_contract_test(nr_load_backend_decode_contract_test nr_load_backend_decode_contract_test.cpp "fast;cpu;unit;load"
    nrload
    utils
)

nr_add_contract_test(nr_load_exr_environment_contract_test nr_load_exr_environment_contract_test.cpp "fast;cpu;unit;load;openexr"
    nrload
    utils
    dependency_assets
    dependency_graphics
)
