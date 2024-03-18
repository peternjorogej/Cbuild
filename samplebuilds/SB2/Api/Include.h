
#define API_VERSION_ID_1 0
#define API_VERSION_ID_2 1
#define API_VERSION_ID_3 2

using IApiInterface = void;

IApiInterface* ApiInit(int iVersionId) noexcept;
bool           ApiShutdown(IApiInterface* pApi) noexcept;



