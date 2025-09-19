# CMakeLists.txt at repo root (same level as zephyr/)
# Register our source file with the ZMK app build.
if(TARGET app)
  target_sources(app PRIVATE
    app/src/behaviors/behavior_oneshot_layer.c
  )
  
endif()
