# Defines weivel_c_write_link_graph_at_the_end(file), which writes the link graph calls_nothing_of_the_standard.cmake
# reads once the whole project has been configured, so a link made anywhere, at any depth or after the tests were
# added, is in it.
#
# Each line is `<origin> <target> <item>,<item>`: `origin` is `own` for a target this repository defines, `other` for
# one a project it adds defines (the core, the standard), and `imported` for an imported target; the items are every
# link item, link option and link flag the target carries or hands on, as written, generator expressions and all, the
# target each alias among them stands for, and for an imported target the files it stands for.

# Sets `${result}` to `directory` and every directory added beneath it, at any depth.
function(weivel_c_directories_under directory result)
    set(found "${directory}")
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        weivel_c_directories_under("${child}" below)
        list(APPEND found ${below})
    endforeach()
    set(${result} "${found}" PARENT_SCOPE)
endfunction()

function(weivel_c_origin_of target root result)
    get_target_property(imported ${target} IMPORTED)
    get_target_property(source ${target} SOURCE_DIR)
    string(FIND "${source}/" "${root}/" at)
    string(FIND "${source}/" "${root}/siblings/" in_siblings)
    if(imported)
        set(${result} imported PARENT_SCOPE)
    elseif(at EQUAL 0 AND NOT in_siblings EQUAL 0)
        set(${result} own PARENT_SCOPE)
    else()
        set(${result} other PARENT_SCOPE)
    endif()
endfunction()

# Appends to `items` the value of each property of `target` the arguments after it name.
macro(weivel_c_append_properties target)
    foreach(property ${ARGN})
        get_target_property(values ${target} ${property})
        if(values AND NOT values MATCHES "-NOTFOUND$")
            list(APPEND items ${values})
        endif()
    endforeach()
endmacro()

function(weivel_c_links_of target result)
    set(items "")
    weivel_c_append_properties(${target} LINK_LIBRARIES INTERFACE_LINK_LIBRARIES LINK_OPTIONS INTERFACE_LINK_OPTIONS
        LINK_FLAGS STATIC_LIBRARY_OPTIONS IMPORTED_LOCATION IMPORTED_IMPLIB IMPORTED_LIBNAME
        IMPORTED_LINK_INTERFACE_LIBRARIES IMPORTED_LINK_DEPENDENT_LIBRARIES)
    get_target_property(configurations ${target} IMPORTED_CONFIGURATIONS)
    foreach(configuration IN LISTS configurations)
        string(TOUPPER "${configuration}" upper)
        weivel_c_append_properties(${target} IMPORTED_LOCATION_${upper} IMPORTED_IMPLIB_${upper}
            IMPORTED_LINK_INTERFACE_LIBRARIES_${upper})
    endforeach()
    foreach(item IN LISTS items)
        if(TARGET "${item}")
            get_target_property(aliased "${item}" ALIASED_TARGET)
            if(aliased)
                list(APPEND items "${aliased}")
            endif()
        endif()
    endforeach()
    string(REPLACE ";" "," items "${items}")
    set(${result} "${items}" PARENT_SCOPE)
endfunction()

function(weivel_c_write_link_graph file root)
    weivel_c_directories_under("${CMAKE_SOURCE_DIR}" directories)
    set(lines "")
    foreach(directory IN LISTS directories)
        get_property(built DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        get_property(imported DIRECTORY "${directory}" PROPERTY IMPORTED_TARGETS)
        foreach(target IN LISTS built imported)
            weivel_c_origin_of(${target} "${root}" origin)
            weivel_c_links_of(${target} links)
            string(APPEND lines "${origin} ${target} ${links}\n")
        endforeach()
    endforeach()
    file(WRITE "${file}" "${lines}")
endfunction()

function(weivel_c_write_link_graph_at_the_end file)
    if(CMAKE_VERSION VERSION_LESS 3.21)
        message(FATAL_ERROR "the tests need CMake 3.21 or later, which lists a directory's imported targets and calls a "
            "function once the whole project is configured; without them the link graph would miss targets")
    endif()
    weivel_c_call_at_the_end(weivel_c_write_link_graph "${file}" "${PROJECT_SOURCE_DIR}")
endfunction()

# Calls `function` with the arguments after it once the whole project is configured, the arguments as they are now:
# a deferred call reads its arguments' variables only when it runs, so they are spelled into the call first.
function(weivel_c_call_at_the_end function)
    set(arguments "")
    foreach(argument IN LISTS ARGN)
        string(APPEND arguments " [==[${argument}]==]")
    endforeach()
    cmake_language(EVAL CODE
        "cmake_language(DEFER DIRECTORY [==[${CMAKE_SOURCE_DIR}]==] CALL ${function}${arguments})")
endfunction()
