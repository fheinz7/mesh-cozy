cmake_minimum_required(VERSION 3.20)
project(nrf_mesh_discovery C)

add_library(mesh_discovery
    src/discovery.c
    src/discovery_protocol.c
    src/discovery_topology.c
    src/relay_watchdog.c)
target_include_directories(mesh_discovery PUBLIC include)
target_compile_features(mesh_discovery PUBLIC c_std_99)
if(MSVC)
  target_compile_options(mesh_discovery PRIVATE /W4)
else()
  target_compile_options(mesh_discovery PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(DISCOVERY_BUILD_EXAMPLES "Build portable gateway and node examples" ON)
if(DISCOVERY_BUILD_EXAMPLES)
  add_executable(discovery_gateway_example examples/gateway_example.c)
  target_link_libraries(discovery_gateway_example PRIVATE mesh_discovery)
  add_executable(discovery_node_example examples/node_example.c)
  target_link_libraries(discovery_node_example PRIVATE mesh_discovery)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(mesh_discovery_tests tests/test_discovery.c)
  target_link_libraries(mesh_discovery_tests PRIVATE mesh_discovery)
  add_test(NAME mesh_discovery_tests COMMAND mesh_discovery_tests)
endif()
