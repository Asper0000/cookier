#pragma once
#include <string>

#ifdef PROVIDER_EXPORTS
#define PROVIDER_API __declspec(dllexport)
#else
#define PROVIDER_API __declspec(dllimport)
#endif

extern "C" {
    PROVIDER_API bool ExtractCookies(char** outJson, const char* browserName, const char* profileList, bool verbose);
}