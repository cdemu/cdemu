# Converts a CMake list to a string containing elements separated by spaces
function (TO_LIST_SPACES LIST_NAME OUTPUT_VAR)
    set (NEW_LIST_SPACE)
    foreach (ITEM ${${LIST_NAME}})
        set (NEW_LIST_SPACE "${NEW_LIST_SPACE} ${ITEM}")
    endforeach ()
    string (STRIP "${NEW_LIST_SPACE}" NEW_LIST_SPACE)
    set (${OUTPUT_VAR} "${NEW_LIST_SPACE}" PARENT_SCOPE)
endfunction ()

