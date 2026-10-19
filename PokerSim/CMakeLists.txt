cmake_minimum_required(VERSION 3.16)
project(PokerSim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_library(pokersim_core STATIC
  src/cards.cpp src/equity.cpp src/config.cpp src/engine.cpp src/brains.cpp src/runner.cpp
  src/ohlang.cpp src/shanky.cpp src/pokerval.cpp src/handrank.cpp src/handrank_data.cpp src/prwin.cpp
  src/ohsymbols_core.cpp src/ohsymbols_table.cpp src/ohbrain.cpp)
target_include_directories(pokersim_core PUBLIC include PRIVATE third_party)
target_link_libraries(pokersim_core PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(pokersim_core PUBLIC /utf-8 /W4 /permissive- /EHsc)
  target_compile_definitions(pokersim_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(pokersim_core PUBLIC -Wall -Wextra)
endif()

add_executable(pokersim apps/cli/main.cpp)
target_link_libraries(pokersim PRIVATE pokersim_core)

enable_testing()
add_executable(pokersim_tests tests/test_core.cpp)
target_link_libraries(pokersim_tests PRIVATE pokersim_core)
add_test(NAME core COMMAND pokersim_tests)

add_executable(ohdump apps/ohdump/main.cpp)
target_link_libraries(ohdump PRIVATE pokersim_core)

add_executable(ohlang_tests tests/test_ohlang.cpp)
target_link_libraries(ohlang_tests PRIVATE pokersim_core)
add_test(NAME ohlang COMMAND ohlang_tests)

# ---------------------------------------------------------------------------
# Oracle (optionnel) : exécute le code ORIGINAL d'OpenHoldem (extrait de CSymbolEnginePokerval.cpp) + poker-eval
# et le compare au port C++ sur des millions de tirages.
#   cmake -S . -B build -DPOKERSIM_OH_ROOT=/chemin/vers/OH15-by-nolog
# ---------------------------------------------------------------------------
set(POKERSIM_OH_ROOT "" CACHE PATH "Racine du dépôt OH15 (active les tests différentiels contre le code original)")
if(POKERSIM_OH_ROOT AND EXISTS "${POKERSIM_OH_ROOT}/pokereval/lib/rules_std.c")
  enable_language(C)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(PE "${POKERSIM_OH_ROOT}/pokereval")
  set(ORC "${CMAKE_BINARY_DIR}/oracle")
  file(MAKE_DIRECTORY "${ORC}")
  file(GLOB PE_TABLES "${PE}/lib/t_*.c")
  set(PE_SRC "")
  foreach(f ${PE_TABLES} "${PE}/lib/deck_std.c" "${PE}/lib/rules_std.c" "${PE}/lib/deck.c")
    get_filename_component(n "${f}" NAME)
    file(READ "${f}" txt)
    string(REGEX REPLACE "([0-9a-fA-F])i64" "\\1ULL" txt "${txt}")   # suffixe MSVC non portable
    file(WRITE "${ORC}/${n}" "${txt}")
    list(APPEND PE_SRC "${ORC}/${n}")
  endforeach()
  add_library(pokereval_oracle STATIC ${PE_SRC})
  target_include_directories(pokereval_oracle PUBLIC "${PE}/include")
  if(NOT MSVC)
    target_compile_options(pokereval_oracle PRIVATE -w)
  endif()
  add_custom_command(OUTPUT "${ORC}/oracle_gen.inc"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/oracle/gen_oracle.py" "${POKERSIM_OH_ROOT}" "${ORC}/oracle_gen.inc"
    DEPENDS "${POKERSIM_OH_ROOT}/OpenHoldem/CSymbolEnginePokerval.cpp" "${CMAKE_SOURCE_DIR}/tests/oracle/gen_oracle.py")
  add_executable(oracle_tests tests/oracle/oracle.cpp tests/oracle/test_oracle.cpp "${ORC}/oracle_gen.inc")
  target_include_directories(oracle_tests PRIVATE "${ORC}")
  target_link_libraries(oracle_tests PRIVATE pokersim_core pokereval_oracle)
  if(NOT MSVC)
    set_source_files_properties(tests/oracle/oracle.cpp PROPERTIES COMPILE_OPTIONS "-w")
  endif()
  add_test(NAME oracle_pokerval COMMAND oracle_tests 1000000)
  add_test(NAME symbols_vs_python COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/symdiff.py $<TARGET_FILE:symdump> 400 1)
endif()

add_executable(prwin_tests tests/test_prwin.cpp)
target_link_libraries(prwin_tests PRIVATE pokersim_core)
add_test(NAME prwin COMMAND prwin_tests)

add_executable(symdump apps/symdump/main.cpp)
target_include_directories(symdump PRIVATE third_party)
target_link_libraries(symdump PRIVATE pokersim_core)
