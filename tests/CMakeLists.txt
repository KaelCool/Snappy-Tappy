# Each test file becomes its own executable and its own ctest entry.
function(add_unit_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE snaptap_core)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(test_key_codes)
add_unit_test(test_engine)
add_unit_test(test_config)
add_unit_test(test_layout)
