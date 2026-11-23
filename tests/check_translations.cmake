if(NOT DEFINED TS_FILE OR NOT DEFINED QM_FILE)
    message(FATAL_ERROR "TS_FILE and QM_FILE are required")
endif()

foreach(path IN ITEMS "${TS_FILE}" "${QM_FILE}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "Missing ${path}. Build the sensevoice-ui target (release_translations) first.")
    endif()
endforeach()

file(READ "${TS_FILE}" ts)
string(REGEX MATCHALL "<context>" contexts "${ts}")
string(REGEX MATCHALL "<name>SpeakAnything</name>" names "${ts}")
list(LENGTH contexts context_count)
list(LENGTH names name_count)
if(NOT context_count EQUAL 1 OR NOT name_count EQUAL 1)
    message(FATAL_ERROR
        "Expected one SpeakAnything context, found ${context_count} context(s) and ${name_count} name(s).")
endif()

string(REGEX MATCHALL "type=\"unfinished\"" unfinished "${ts}")
string(REGEX MATCHALL "type=\"vanished\"" vanished "${ts}")
list(LENGTH unfinished unfinished_count)
list(LENGTH vanished vanished_count)
if(NOT unfinished_count EQUAL 0)
    message(FATAL_ERROR "Translation catalogue has ${unfinished_count} unfinished entries.")
endif()
if(NOT vanished_count EQUAL 0)
    message(FATAL_ERROR "Translation catalogue has ${vanished_count} vanished entries.")
endif()

file(SIZE "${QM_FILE}" qm_size)
if(qm_size EQUAL 0)
    message(FATAL_ERROR "Compiled translation is empty: ${QM_FILE}")
endif()

message(STATUS
    "translations-check: 1 context, 0 unfinished, 0 vanished, ${qm_size}-byte .qm")
