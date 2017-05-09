# Downloaded from https://github.com/larsch/cmake-precompiled-header, and fixed for our needs.
# Function for setting up precompiled headers. Usage:
#
#   add_library/executable(target
#       pchheader.c pchheader.cpp pchheader.h)
#
#   add_precompiled_header(target pchheader.h
#       [FORCEINCLUDE]
#       [SOURCE_C pchheader.c]
#       [SOURCE_CXX pchheader.cpp])
#
# Options:
#
#   FORCEINCLUDE: Add compiler flags to automatically include the
#   pchheader.h from every source file. Works with both GCC and
#   MSVC. This is recommended.
#
#   SOURCE_C/CXX: Specifies the .c/.cpp source file that includes
#   pchheader.h for generating the pre-compiled header
#   output. Defaults to pchheader.c. Only required for MSVC.
#
# Caveats:
#
#   * Its not currently possible to use the same precompiled-header in
#     more than a single target in the same directory (No way to set
#     the source file properties differently for each target).
#
#   * MSVC: A source file with the same name as the header must exist
#     and be included in the target (E.g. header.cpp). Name of file
#     can be changed using the SOURCE_CXX/SOURCE_C options.
#
# License:
#
# Copyright (C) 2009-2017 Lars Christensen <larsch@belunktum.dk>
#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation files
# (the 'Software') deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge,
# publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

include(CMakeParseArguments)

macro(combine_arguments _variable)
  set(_result "")
  foreach(_element ${${_variable}})
    set(_result "${_result} \"${_element}\"")
  endforeach()
  string(STRIP "${_result}" _result)
  set(${_variable} "${_result}")
endmacro()

function(export_all_flags _filename)
  set(_include_directories "$<TARGET_PROPERTY:${_target},INCLUDE_DIRECTORIES>")
  set(_compile_definitions "$<TARGET_PROPERTY:${_target},COMPILE_DEFINITIONS>")
  set(_compile_flags "$<TARGET_PROPERTY:${_target},COMPILE_FLAGS>")
  set(_compile_options "$<TARGET_PROPERTY:${_target},COMPILE_OPTIONS>")
  set(_include_directories
    "$<$<BOOL:${_include_directories}>:-I$<JOIN:${_include_directories},\n-I>\n>")
  set(_compile_definitions
    "$<$<BOOL:${_compile_definitions}>:-D$<JOIN:${_compile_definitions},\n-D>\n>")
  set(_compile_flags "$<$<BOOL:${_compile_flags}>:$<JOIN:${_compile_flags},\n>\n>")
  set(_compile_options "$<$<BOOL:${_compile_options}>:$<JOIN:${_compile_options},\n>\n>")
  file(GENERATE OUTPUT
    "${_filename}"
    CONTENT "${_compile_definitions}${_include_directories}${_compile_flags}${_compile_options}\n")
endfunction()

function(add_precompiled_header _target _input)
  cmake_parse_arguments(_PCH "FORCEINCLUDE" "SOURCE_CXX;SOURCE_C" "" ${ARGN})

  get_filename_component(_input_we ${_input} NAME_WE)
  if(NOT _PCH_SOURCE_CXX)
    set(_PCH_SOURCE_CXX "${_input_we}.cpp")
  endif()
  if(NOT _PCH_SOURCE_C)
    set(_PCH_SOURCE_C "${_input_we}.c")
  endif()

  get_filename_component(_name ${_input} NAME)
  set(_pch_header "${CMAKE_CURRENT_SOURCE_DIR}/${_input}")
  set(_pch_binary_dir "${CMAKE_CURRENT_BINARY_DIR}/${_target}_pch")
  set(_pchfile "${_pch_binary_dir}/${_input}")
  set(_outdir "${_pch_binary_dir}/${_name}.gch")
  add_custom_command(
    OUTPUT "${_outdir}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_outdir}"
    COMMENT "Creating ${_outdir} for precompiled header")
  set(_output_cxx "${_outdir}/.c++")

  set(_pch_flags_file "${_pch_binary_dir}/compile_flags.rsp")
  export_all_flags("${_pch_flags_file}")
  set(_compiler_FLAGS "@${_pch_flags_file}")
  # We are doing the following trick here:
  # 1) Create precompiled build_dir/.../file.h.gch from source_dir/.../file.h
  # 2) Set -include build_dir/.../file.h to all source files. Really this file is missing,
  #    but compiler is happy because it finds build_dir/.../file.h.gch and uses it.
  add_custom_command(
    OUTPUT "${_output_cxx}"
    COMMAND "${CMAKE_CXX_COMPILER}"
            "$(CXX_FLAGS)"
            ${_compiler_FLAGS}
            -x c++-header
            -o "${_output_cxx}"
            "${_pch_header}"
    DEPENDS "${_pch_header}" "${_pch_flags_file}" "${_outdir}"
    IMPLICIT_DEPENDS CXX "${_pch_header}"
    COMMENT "Precompiling ${_name} for ${_target} (C++)")

  get_property(_sources TARGET ${_target} PROPERTY SOURCES)
  foreach(_source ${_sources})
    set(_pch_compile_flags "")

    if(_source MATCHES \\.\(cc|cxx|cpp|c\)$)
      get_source_file_property(_pch_compile_flags "${_source}" COMPILE_FLAGS)
      if(NOT _pch_compile_flags)
        set(_pch_compile_flags)
      endif()
      separate_arguments(_pch_compile_flags)
      list(APPEND _pch_compile_flags -Winvalid-pch)
      if(_PCH_FORCEINCLUDE)
        list(APPEND _pch_compile_flags -include "${_pchfile}")
      else(_PCH_FORCEINCLUDE)
        list(APPEND _pch_compile_flags "-I${_pch_binary_dir}")
      endif(_PCH_FORCEINCLUDE)

      get_source_file_property(_object_depends "${_source}" OBJECT_DEPENDS)
      if(NOT _object_depends)
        set(_object_depends)
      endif()
     list(APPEND _object_depends "${_output_cxx}")

      combine_arguments(_pch_compile_flags)
      set_source_files_properties(${_source} PROPERTIES
        COMPILE_FLAGS "${_pch_compile_flags}"
        OBJECT_DEPENDS "${_object_depends}")
    endif()
  endforeach()
endfunction()
