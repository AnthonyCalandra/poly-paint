function(poly_paint_report_simd_capabilities)
    if (NOT MSVC)
        message(STATUS "SIMD capability probe is currently implemented for MSVC on Windows only.")
        return()
    endif()

    set(probe_source "${CMAKE_BINARY_DIR}/CMakeFiles/poly_paint_simd_probe.cpp")
    file(WRITE "${probe_source}" [=[
#include <intrin.h>
#include <iostream>

int main()
{
    int registers[4] {};
    __cpuidex(registers, 0, 0);
    const int max_basic_leaf = registers[0];

    bool sse = false;
    bool sse2 = false;
    bool sse3 = false;
    bool ssse3 = false;
    bool sse41 = false;
    bool sse42 = false;
    bool avx = false;
    bool avx2 = false;
    bool avx512f = false;

    if (max_basic_leaf >= 1)
    {
        __cpuidex(registers, 1, 0);
        sse = (registers[3] & (1 << 25)) != 0;
        sse2 = (registers[3] & (1 << 26)) != 0;
        sse3 = (registers[2] & (1 << 0)) != 0;
        ssse3 = (registers[2] & (1 << 9)) != 0;
        sse41 = (registers[2] & (1 << 19)) != 0;
        sse42 = (registers[2] & (1 << 20)) != 0;

        const bool xsave = (registers[2] & (1 << 26)) != 0;
        const bool osxsave = (registers[2] & (1 << 27)) != 0;
        const bool cpu_avx = (registers[2] & (1 << 28)) != 0;
        if (xsave && osxsave && cpu_avx)
        {
            const unsigned __int64 xcr0 = _xgetbv(0);
            avx = (xcr0 & 0x6) == 0x6;
            if (max_basic_leaf >= 7 && avx)
            {
                __cpuidex(registers, 7, 0);
                avx2 = (registers[1] & (1 << 5)) != 0;
                avx512f = (registers[1] & (1 << 16)) != 0 && (xcr0 & 0xE6) == 0xE6;
            }
        }
    }

    std::cout
        << "SSE=" << sse << '\n'
        << "SSE2=" << sse2 << '\n'
        << "SSE3=" << sse3 << '\n'
        << "SSSE3=" << ssse3 << '\n'
        << "SSE4_1=" << sse41 << '\n'
        << "SSE4_2=" << sse42 << '\n'
        << "AVX=" << avx << '\n'
        << "AVX2=" << avx2 << '\n'
        << "AVX512F=" << avx512f << '\n';
}
]=])

    try_run(
        SIMD_PROBE_EXIT_CODE
        SIMD_PROBE_COMPILED
        "${CMAKE_BINARY_DIR}/CMakeFiles/poly_paint_simd_probe"
        "${probe_source}"
        RUN_OUTPUT_VARIABLE SIMD_PROBE_OUTPUT)

    if (SIMD_PROBE_COMPILED AND SIMD_PROBE_EXIT_CODE EQUAL 0)
        message(STATUS "Detected SIMD capabilities (CPU and OS enabled):\n${SIMD_PROBE_OUTPUT}")
    else()
        message(WARNING "Could not compile or run the SIMD capability probe.")
    endif()
endfunction()
