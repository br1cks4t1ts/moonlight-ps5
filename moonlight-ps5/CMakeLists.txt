cmake_minimum_required(VERSION 3.16)
project(moonlight-ps5 LANGUAGES C)

set(CMAKE_C_STANDARD 11)

# ── Paths ────────────────────────────────────────────────────────────────────
set(ROOT      ${CMAKE_SOURCE_DIR}/..)
set(SDK_BUILD ${ROOT}/build)

set(MOONLIGHT_SRC  ${ROOT}/moonlight-common-c)
set(MBEDTLS_INC    ${ROOT}/mbedtls/include)
set(OPUS_INC       ${ROOT}/opus/include)   # headers live at include/opus_multistream.h
set(FFMPEG_INC     ${SDK_BUILD}/ffmpeg/include)

set(MOONLIGHT_LIB  ${SDK_BUILD}/moonlight-common-c/libmoonlight-common-c.a)
set(ENET_LIB       ${SDK_BUILD}/moonlight-common-c/enet/libenet.a)
set(MBEDTLS_LIB    ${SDK_BUILD}/mbedtls/library/libmbedtls.a)
set(MBEDX509_LIB   ${SDK_BUILD}/mbedtls/library/libmbedx509.a)
set(MBEDCRYPTO_LIB ${SDK_BUILD}/mbedtls/library/libmbedcrypto.a)
set(OPUS_LIB       ${SDK_BUILD}/opus/libopus.a)
set(AVCODEC_LIB    ${SDK_BUILD}/ffmpeg/lib/libavcodec.a)
set(AVUTIL_LIB     ${SDK_BUILD}/ffmpeg/lib/libavutil.a)

# ── Sources ──────────────────────────────────────────────────────────────────
add_executable(moonlight-ps5
    src/main.c
    src/gamestream.c
    src/video.c
    src/audio.c
    src/input.c
    src/cpu_model_stub.c
)

target_include_directories(moonlight-ps5 PRIVATE
    src
    ${MOONLIGHT_SRC}/src
    ${MBEDTLS_INC}
    ${OPUS_INC}
    ${FFMPEG_INC}
)

target_compile_options(moonlight-ps5 PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -include ${ROOT}/ps5_compat.h
)

target_compile_definitions(moonlight-ps5 PRIVATE
    USE_MBEDTLS
    HAS_SOCKLEN_T
)

# ── Link ─────────────────────────────────────────────────────────────────────
target_link_libraries(moonlight-ps5 PRIVATE
    ${MOONLIGHT_LIB}
    ${ENET_LIB}
    ${MBEDTLS_LIB}
    ${MBEDX509_LIB}
    ${MBEDCRYPTO_LIB}
    ${OPUS_LIB}
    ${AVCODEC_LIB}
    ${AVUTIL_LIB}
    # Export module_start so SYS_sprx_load can call it
    -Wl,--export-dynamic
    # PS5 system libraries (resolved from stub .so at runtime)
    -lSceVideoOut
    -lSceAudioOut
    -lScePad
    -lSceUserService
    -lSceSysmodule
    -lSceNet
    -lpthread
)

# Output as .elf so the SDK deploy scripts recognise it
set_target_properties(moonlight-ps5 PROPERTIES
    OUTPUT_NAME "moonlight-ps5"
    SUFFIX      ".elf"
)

# ── Deploy helper (make deploy HOST=<ip>) ────────────────────────────────────
if(DEFINED PS5_HOST)
    add_custom_target(deploy
        COMMAND ${PS5_PAYLOAD_SDK}/bin/prospero-deploy ${PS5_HOST} ${PS5_PORT}
                moonlight-ps5.elf
        DEPENDS moonlight-ps5
        COMMENT "Pushing moonlight-ps5.elf to PS5 at ${PS5_HOST}:${PS5_PORT}"
    )
endif()
