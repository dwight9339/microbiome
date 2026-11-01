# mb_add_pack(NAME PACK_DIR OUTPUT) - compile a pack directory with packc.
function(mb_add_pack name pack_dir output)
    get_filename_component(_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    file(GLOB_RECURSE _inputs CONFIGURE_DEPENDS "${pack_dir}/*.yaml" "${pack_dir}/*.png")
    add_custom_command(
        OUTPUT ${output}
        COMMAND ${Python3_EXECUTABLE} ${_root}/tools/packc/packc.py ${pack_dir} -o ${output}
        DEPENDS ${_inputs} ${_root}/tools/packc/packc.py
        COMMENT "packc: ${name}"
        VERBATIM)
endfunction()
