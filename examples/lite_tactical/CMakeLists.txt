set(APP_NAME rocky_lite_tactical)

add_executable(${APP_NAME}
    main.cpp
    SimContext.h
    SceneSync.h
    SceneSync.cpp
    DemoScenario.h
    DemoScenario.cpp
    ModelLoader.h
    ModelLoader.cpp
    Primitives.h
)

target_link_libraries(${APP_NAME} rocky)

# src/rocky/CMakeLists.txt makes <rocky/...> resolve via a directory-scoped
# include_directories(), which only reaches targets added from within
# src/'s own CMakeLists.txt tree -- examples/ is a sibling of src/, added
# from the root CMakeLists.txt, so it needs the path added explicitly.
target_include_directories(${APP_NAME} PRIVATE "${PROJECT_SOURCE_DIR}/src")

# tinyobjloader (ModelLoader.cpp) is vendored as a bare header, the same
# way third_party/stb and third_party/cpp-httplib are referenced directly
# by src/tests/CMakeLists.txt -- see third_party/VERSIONS.md.
target_include_directories(${APP_NAME} PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/tinyobjloader")

set_target_properties(${APP_NAME} PROPERTIES FOLDER "examples")
