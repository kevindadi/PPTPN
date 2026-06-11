add_library(scheduling_analyzer STATIC
    dbm.cpp
    scheduler.cpp
    state.cpp
    reachability.cpp
    task_template.cpp
)

target_include_directories(scheduling_analyzer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(scheduling_analyzer PUBLIC
    PTPN_core
)

target_compile_features(scheduling_analyzer PRIVATE cxx_std_17)