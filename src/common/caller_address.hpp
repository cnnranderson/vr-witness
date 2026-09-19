#pragma once

// Must expand inside the detour, not inside an out-of-line helper.
#ifdef _MSC_VER
#include <intrin.h>
#define WITNESS_RETURN_ADDRESS() _ReturnAddress()
#else
#define WITNESS_RETURN_ADDRESS() __builtin_extract_return_addr(__builtin_return_address(0))
#endif
