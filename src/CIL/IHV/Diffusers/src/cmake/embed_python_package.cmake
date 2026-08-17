# embed_python_package.cmake -- Embed multiple .py files into one header.
#
# Walks ${EPP_PACKAGE_DIR} recursively for .py files, embeds each as a
# chunked C string literal (to dodge MSVC C2026), and emits a manifest
# array of {name, source, length} at the end. Basenames listed in
# ${EPP_EXCLUDE} are skipped.
#
# Module qualname is derived from the path relative to ${EPP_PACKAGE_DIR}:
# directory separators become dots and the .py suffix is stripped, so
# `opts/nvfp4.py` -> `opts.nvfp4` and `opts/__init__.py` -> `opts.__init__`.
# The finder in _bootstrap.py expects this convention.
#
# Inputs:
#   EPP_PACKAGE_DIR   - absolute path to scripts directory
#   EPP_OUTPUT_HEADER - absolute path to generated header
#   EPP_NAMESPACE     - colon-separated C++ namespace (e.g. "cil::IHV::embedded")
#   EPP_EXCLUDE       - semicolon list of basenames to skip (optional)
#   EPP_CHUNK_SIZE    - max chars per string literal (default 8000)

if(NOT DEFINED EPP_CHUNK_SIZE)
    set(EPP_CHUNK_SIZE 8000)
endif()

file(GLOB_RECURSE _epp_files RELATIVE "${EPP_PACKAGE_DIR}"
     CONFIGURE_DEPENDS "${EPP_PACKAGE_DIR}/*.py")

set(_epp_kept "")
foreach(_rel IN LISTS _epp_files)
    get_filename_component(_base "${_rel}" NAME)
    set(_skip FALSE)
    foreach(_ex IN LISTS EPP_EXCLUDE)
        if(_base STREQUAL "${_ex}")
            set(_skip TRUE)
            break()
        endif()
    endforeach()
    if(NOT _skip)
        list(APPEND _epp_kept "${_rel}")
    endif()
endforeach()
list(SORT _epp_kept)

set(_out "// Auto-generated -- do not edit manually.\n#pragma once\n\n#include <cstddef>\n\n")

if(DEFINED EPP_NAMESPACE)
    string(REPLACE "::" ";" _ns_parts "${EPP_NAMESPACE}")
    foreach(_ns IN LISTS _ns_parts)
        string(APPEND _out "namespace ${_ns} {\n")
    endforeach()
    string(APPEND _out "\n")
endif()

set(_manifest_entries "")
set(_index 0)
foreach(_rel IN LISTS _epp_kept)
    set(_abs "${EPP_PACKAGE_DIR}/${_rel}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_abs}")

    file(READ "${_abs}" _contents)
    string(REPLACE "\\" "\\\\" _contents "${_contents}")
    string(REPLACE "\"" "\\\"" _contents "${_contents}")
    string(REPLACE "\r" ""     _contents "${_contents}")
    string(REPLACE "\n" "\\n"  _contents "${_contents}")
    string(LENGTH "${_contents}" _len)

    string(REGEX REPLACE "\\.py$" "" _qual "${_rel}")
    string(REPLACE "/"  "." _qual "${_qual}")
    string(REPLACE "\\" "." _qual "${_qual}")

    string(APPEND _out "inline const char kSrc_${_index}[] =\n")

    set(_i 0)
    while(_i LESS _len)
        math(EXPR _remaining "${_len} - ${_i}")
        if(_remaining GREATER ${EPP_CHUNK_SIZE})
            set(_sz ${EPP_CHUNK_SIZE})
        else()
            set(_sz ${_remaining})
        endif()

        string(SUBSTRING "${_contents}" ${_i} ${_sz} _chunk)

        if(_sz LESS _remaining)
            string(REGEX MATCH "[\\\\]+$" _trailing "${_chunk}")
            string(LENGTH "${_trailing}" _tlen)
            math(EXPR _odd "${_tlen} % 2")
            if(_odd)
                math(EXPR _sz "${_sz} - 1")
                string(SUBSTRING "${_contents}" ${_i} ${_sz} _chunk)
            endif()
        endif()

        string(APPEND _out "    \"${_chunk}\"\n")
        math(EXPR _i "${_i} + ${_sz}")
    endwhile()
    string(APPEND _out ";\n\n")

    list(APPEND _manifest_entries
         "    {\"${_qual}\", kSrc_${_index}, sizeof(kSrc_${_index}) - 1}")
    math(EXPR _index "${_index} + 1")
endforeach()

string(APPEND _out "struct EmbeddedSource {\n")
string(APPEND _out "  const char* name;\n")
string(APPEND _out "  const char* source;\n")
string(APPEND _out "  std::size_t length;\n")
string(APPEND _out "};\n\n")

string(APPEND _out "inline const EmbeddedSource kEmbeddedSources[] = {\n")
string(JOIN ",\n" _manifest_body ${_manifest_entries})
string(APPEND _out "${_manifest_body}\n};\n\n")
string(APPEND _out "inline const std::size_t kEmbeddedSourcesCount =\n")
string(APPEND _out "    sizeof(kEmbeddedSources) / sizeof(kEmbeddedSources[0]);\n")

if(DEFINED EPP_NAMESPACE)
    string(APPEND _out "\n")
    list(REVERSE _ns_parts)
    foreach(_ns IN LISTS _ns_parts)
        string(APPEND _out "}  // namespace ${_ns}\n")
    endforeach()
endif()

file(WRITE "${EPP_OUTPUT_HEADER}" "${_out}")
