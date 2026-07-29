nr_add_contract_test(nr_interaction_websocket_contract_test nr_interaction_websocket_contract_test.cpp "fast;cpu;unit;interaction"
    nrinteraction
    nroptions
    dependency_services
    dependency_network_test
    utils
)
set_tests_properties(nr_interaction_websocket_contract_test PROPERTIES TIMEOUT 30)
