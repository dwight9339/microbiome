add_executable(mbsim main.c)
target_link_libraries(mbsim PRIVATE mb_core)
if(TARGET SDL2::SDL2)
    target_link_libraries(mbsim PRIVATE SDL2::SDL2)
    # Windows: put SDL2.dll next to mbsim.exe so it runs straight from the build dir.
    if(WIN32)
        get_target_property(_sdl_type SDL2::SDL2 TYPE)
        if(_sdl_type STREQUAL "SHARED_LIBRARY")
            add_custom_command(TARGET mbsim POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:mbsim>
                VERBATIM)
        endif()
    endif()
else()
    target_include_directories(mbsim PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(mbsim PRIVATE ${SDL2_LIBRARIES})
endif()
