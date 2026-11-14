# Defines read_tokens(text result literals), which reads a C source as the checks under test/ need it.

# Sets `${result}` to the tokens of `text`, each between two spaces: identifiers and `##`; and `${literals}` to each run
# of adjacent string literals, joined into one as the compiler joins them. Comments and character literals are matched
# whole and dropped.
function(read_tokens text result literals)
    string(REGEX REPLACE "[][;]" " " text "${text}")
    set(block_comment "/\\*([^*]|\\*+[^*/])*\\*+/")
    set(line_comment "//[^\n]*")
    set(string_literal "\"([^\"\\\\\n]|\\\\.)*\"")
    set(character_literal "'([^'\\\\\n]|\\\\.)*'")
    string(REGEX MATCHALL
        "${block_comment}|${line_comment}|${string_literal}|${character_literal}|[A-Za-z_][A-Za-z0-9_]*|##|[^ \t\r\n]"
        matched "${text}")
    set(tokens "")
    set(joined_literals "")
    set(joined "")
    set(in_literal FALSE)
    foreach(token IN LISTS matched)
        if(token MATCHES "^\"(.*)\"$")
            string(APPEND joined "${CMAKE_MATCH_1}")
            set(in_literal TRUE)
        elseif(NOT token MATCHES "^(/\\*|//)")
            if(in_literal)
                list(APPEND joined_literals "${joined}")
                set(joined "")
                set(in_literal FALSE)
            endif()
            if(token MATCHES "^([A-Za-z_][A-Za-z0-9_]*|##)$")
                string(APPEND tokens " ${token} ")
            endif()
        endif()
    endforeach()
    if(in_literal)
        list(APPEND joined_literals "${joined}")
    endif()
    set(${result} " ${tokens} " PARENT_SCOPE)
    set(${literals} "${joined_literals}" PARENT_SCOPE)
endfunction()
