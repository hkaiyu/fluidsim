#pragma once
#include <stdint.h>

struct CompiledSpirvShader
{
    const char* entry = nullptr;
    const uint32_t* pCode = nullptr;
    size_t size = 0;
};
