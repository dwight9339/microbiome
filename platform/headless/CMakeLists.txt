add_executable(mbsnap main.c png.c)
target_link_libraries(mbsnap PRIVATE mb_core)
