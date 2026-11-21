cmake_minimum_required(VERSION 3.16)
project(FA_Lab3 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(task01 task01.c)
add_executable(task02 task02.c)
add_executable(task03 task03.c)
add_executable(task04 task04.c)
add_executable(task05 task05.c)
add_executable(task06 task06.c)
add_executable(task07 task07.c)
add_executable(task08 task08.c)
add_executable(task09 task09.c)
add_executable(task10 task10.c)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(task01 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(task02 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(task03 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(task04 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(task05 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(task06 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(task07 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(task08 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(task09 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(task10 PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(UNIX)
    target_link_libraries(task06 PRIVATE m)
    target_link_libraries(task07 PRIVATE m)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_test(NAME task01_checks
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_task01.py"
                         "$<TARGET_FILE:task01>" "${CMAKE_C_COMPILER}")
        add_test(NAME task02_checks
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_task02.py"
                         "$<TARGET_FILE:task02>" "${CMAKE_C_COMPILER}")
        add_test(NAME task03_checks
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_task03.py"
                         "$<TARGET_FILE:task03>" "${CMAKE_C_COMPILER}")
        add_test(NAME task04_checks
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_task04.py"
                         "$<TARGET_FILE:task04>" "${CMAKE_C_COMPILER}")
        add_test(NAME task05_checks
                 COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_task05.py"
                         "$<TARGET_FILE:task05>" "${CMAKE_C_COMPILER}")
    endif()
endif()
