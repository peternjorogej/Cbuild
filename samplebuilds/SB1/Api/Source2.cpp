
#include "Include.h"
#include <stdio.h>

bool ApiShutdown(IApiInterface* pApi) noexcept
{
    printf("[DEBUG]: %s(%s)\n", __FUNCTION__, (const char*)pApi);
    return true;
}


