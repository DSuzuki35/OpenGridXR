// Owned test DLL: verify the worker pins its containing module independently of
// the loader's initial reference, then releases it after explicit stop/drain.
#include "../async_log.hpp"
extern "C" __declspec(dllexport) void fixture_start_log(){lmuqv::async_log_enqueue("lifetime fixture\r\n",18);}
extern "C" __declspec(dllexport) void fixture_stop_log(){lmuqv::async_log_stop();}
