
#include "Api/Include.h"
#include <stdio.h>

int main(int iArgc, char* ppArgv[])
{
    (void)(iArgc, ppArgv);
    if (IApiInterface* pApi = ApiInit(API_VERSION_ID_2))
    {
        ApiShutdown(pApi);
    }

    return printf("\n"), 0;
}

