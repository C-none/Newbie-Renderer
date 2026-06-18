nr_add_contract_test(nr_rhi_device_capability_contract_test nr_rhi_device_capability_contract_test.cpp "gpu;window;integration;rhi"
    nrrhi
    utils
    dependency
)

nr_add_contract_test(nr_rhi_upload_readback_buffer_roundtrip_test nr_rhi_upload_readback_buffer_roundtrip_test.cpp "gpu;window;integration;rhi;upload;readback"
    nrrhi
    utils
    dependency
)
