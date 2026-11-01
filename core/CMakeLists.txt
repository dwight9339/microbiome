# microbiome core: portable C11, no heap, no platform dependencies.
add_library(mb_core STATIC
    src/pack.c
    src/world.c
    src/actors.c
    src/particles.c
    src/engine.c
    src/render.c
)
target_include_directories(mb_core PUBLIC include)
set_target_properties(mb_core PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mb_core PRIVATE -Wall -Wextra -Wshadow -Wno-unused-parameter)
endif()
