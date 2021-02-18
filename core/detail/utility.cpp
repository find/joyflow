#include "utility.h"
#include <xxhash.h>

BEGIN_JOYFLOW_NAMESPACE

CORE_API size_t xxhash(void const* data, size_t size)
{
  if (sizeof(size_t) == sizeof(int32_t)) {
    return XXH32(data, size, 0);
  } else {
    return XXH64(data, size, 0);
  }
}

END_JOYFLOW_NAMESPACE
