# Parity runner: links bw_core, reads contract/cases.json, writes outputs/<id>.json
# in the schema contract/compare.py expects.
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_executable(parity_runner parity_runner.cpp)
target_link_libraries(parity_runner PRIVATE bw_core nlohmann_json::nlohmann_json)
