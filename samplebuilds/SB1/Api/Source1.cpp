
#include "Include.h"
#include <stdio.h>

IApiInterface* ApiInit(int iVersionId) noexcept
{
    static constexpr const char* s_Versions[] =
    {
        [API_VERSION_ID_1] = "API_VERSION_ID_1",
        [API_VERSION_ID_2] = "API_VERSION_ID_2",
        [API_VERSION_ID_3] = "API_VERSION_ID_3",
    };

    printf("[DEBUG]: %s(%s)\n", __FUNCTION__, s_Versions[iVersionId]);
    return (IApiInterface*)s_Versions[iVersionId];
}


