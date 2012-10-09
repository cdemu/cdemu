# Converts a CMake list to a string containing elements separated by spaces
function (TO_LIST_SPACES LIST_NAME OUTPUT_VAR)
    set (NEW_LIST_SPACE)
    foreach (ITEM ${${LIST_NAME}})
        set (NEW_LIST_SPACE "${NEW_LIST_SPACE} ${ITEM}")
    endforeach ()
    string (STRIP ${NEW_LIST_SPACE} NEW_LIST_SPACE)
    set (${OUTPUT_VAR} "${NEW_LIST_SPACE}" PARENT_SCOPE)
endfunction ()

# Appends a list of items to a string which is a space-separated list, if the items don't already exist.
function (LIST_SPACES_APPEND_ONCE LIST_NAME)
    string (REPLACE " " ";" _LIST ${${LIST_NAME}})
    list (APPEND _LIST ${ARGN})
    list (REMOVE_DUPLICATES _LIST)
    to_list_spaces (_LIST NEW_LIST_SPACE)
    set (${LIST_NAME} "${NEW_LIST_SPACE}" PARENT_SCOPE)
endfunction ()

