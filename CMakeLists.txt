cmake_minimum_required(VERSION 3.16)
project(hms_gmail CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -pthread")

option(BUILD_TESTS "Build unit tests" ON)

# ── hms-shared ────────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(hms_shared
    GIT_REPOSITORY https://github.com/hms-homelab/hms-shared.git
    GIT_TAG        v1.6.6
)
FetchContent_Populate(hms_shared)

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(Drogon CONFIG REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(PahoMqttCpp REQUIRED)
pkg_check_modules(VMIME REQUIRED vmime)
pkg_check_modules(YAML_CPP REQUIRED yaml-cpp)

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/config/AppConfig.cpp
    src/db/Database.cpp
    src/mqtt/MqttClient.cpp
    src/backup/BackupManager.cpp
    src/mime/MimeParser.cpp
    src/indexer/Indexer.cpp
    src/embedding/EmbeddingWorker.cpp
    src/search/SearchEngine.cpp
    src/gmail/OAuthManager.cpp
    src/gmail/GmailClient.cpp
    src/gmail/SyncManager.cpp
    src/api/controllers/HealthController.cpp
    src/api/controllers/BackupController.cpp
    src/api/controllers/SearchController.cpp
    ${hms_shared_SOURCE_DIR}/mqtt/src/mqtt_client.cpp
    ${hms_shared_SOURCE_DIR}/db/src/db_pool.cpp
    ${hms_shared_SOURCE_DIR}/llm/src/llm_client.cpp
)

add_executable(hms_gmail ${SOURCES})

target_include_directories(hms_gmail PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${hms_shared_SOURCE_DIR}/mqtt/include
    ${hms_shared_SOURCE_DIR}/db/include
    ${hms_shared_SOURCE_DIR}/llm/include
    ${VMIME_INCLUDE_DIRS}
    ${YAML_CPP_INCLUDE_DIRS}
)

target_link_libraries(hms_gmail PRIVATE
    Drogon::Drogon
    jsoncpp_lib
    OpenSSL::SSL OpenSSL::Crypto
    PahoMqttCpp::paho-mqttpp3
    ${VMIME_LIBRARIES}
    ${YAML_CPP_LIBRARIES}
    pq pqxx
    fmt curl
    sqlite3
    z
    pthread
)

# ── Tests ─────────────────────────────────────────────────────────────────────
option(ENABLE_COVERAGE "Build with gcov coverage instrumentation" OFF)

if(BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    set(TEST_SOURCES
        tests/main.cpp
        tests/test_config.cpp
        tests/test_mime_parser.cpp
        tests/test_embedding_worker.cpp
        tests/test_indexer.cpp
        tests/test_search_engine.cpp
        tests/test_oauth_manager.cpp
        tests/test_gmail_client.cpp
        src/config/AppConfig.cpp
        src/db/Database.cpp
        src/mime/MimeParser.cpp
        src/indexer/Indexer.cpp
        src/embedding/EmbeddingWorker.cpp
        src/search/SearchEngine.cpp
        src/gmail/OAuthManager.cpp
        src/gmail/GmailClient.cpp
        ${hms_shared_SOURCE_DIR}/db/src/db_pool.cpp
    )

    add_executable(run_tests ${TEST_SOURCES})
    target_include_directories(run_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${hms_shared_SOURCE_DIR}/db/include
        ${YAML_CPP_INCLUDE_DIRS}
        ${GTEST_INCLUDE_DIRS}
        ${VMIME_INCLUDE_DIRS}
    )
    target_link_libraries(run_tests PRIVATE
        GTest::GTest GTest::Main
        ${YAML_CPP_LIBRARIES}
        ${VMIME_LIBRARIES}
        jsoncpp_lib
        pq pqxx fmt curl sqlite3 z pthread
    )

    if(ENABLE_COVERAGE)
        target_compile_options(run_tests PRIVATE --coverage -O0 -g)
        target_link_options(run_tests PRIVATE --coverage)
    endif()

    add_test(NAME run_tests COMMAND run_tests)

    # ── Coverage target (requires lcov + genhtml) ──────────────────────────────
    if(ENABLE_COVERAGE)
        find_program(LCOV lcov)
        find_program(GENHTML genhtml)
        if(LCOV AND GENHTML)
            add_custom_target(coverage
                COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target run_tests
                COMMAND ${CMAKE_BINARY_DIR}/run_tests
                COMMAND ${LCOV}
                    --capture
                    --directory ${CMAKE_BINARY_DIR}
                    --output-file ${CMAKE_BINARY_DIR}/coverage.info
                    --no-external
                    --base-directory ${CMAKE_SOURCE_DIR}
                COMMAND ${LCOV}
                    --remove ${CMAKE_BINARY_DIR}/coverage.info
                    "*/_deps/*" "*/tests/*"
                    --output-file ${CMAKE_BINARY_DIR}/coverage_filtered.info
                COMMAND ${GENHTML}
                    ${CMAKE_BINARY_DIR}/coverage_filtered.info
                    --output-directory ${CMAKE_BINARY_DIR}/coverage_html
                    --legend --title "hms-gmail"
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Generating coverage report → build/coverage_html/index.html"
            )
        endif()
    endif()
endif()
