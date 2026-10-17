# Minimal app: the smallest program that drives the engine directly (no Scene/editor).
add_executable(Example
    main.cpp
    ExampleLayer.cpp
)
target_link_libraries(Example PRIVATE phoenix)
phoenix_copy_assets(Example)
