#!/usr/bin/env python3
import re

with open('CmakeLists.txt', 'r') as f:
    content = f.read()

# Find where add_test section starts
add_test_pos = content.find('add_test(')

if add_test_pos > 0:
    # Insert Phase 3 test executable definition before add_test section
    phase3_test = """nr_add_executable(nr_renderer_phase3_semantic_hardening_test nr_renderer_phase3_semantic_hardening_test.cpp)
set_target_properties(nr_renderer_phase3_semantic_hardening_test PROPERTIES FOLDER "Test/profile")

target_link_libraries(nr_renderer_phase3_semantic_hardening_test PRIVATE
    nrrenderpasses
    utils
    dependency
)

"""
    
    content = content[:add_test_pos] + phase3_test + content[add_test_pos:]
    
    with open('CmakeLists.txt', 'w') as f:
        f.write(content)
    
    print('Successfully added Phase 3 test executable definition')
else:
    print('Could not find add_test section')
