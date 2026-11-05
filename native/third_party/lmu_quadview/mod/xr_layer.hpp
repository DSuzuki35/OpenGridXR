#pragma once
#include "bridge.hpp"
#include "openxr/openxr_platform.h"
#include "openxr/openxr_loader_negotiation.h"

#ifndef OPENGRIDXR_LMU_INTERNAL
extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo*, const char*, XrNegotiateApiLayerRequest*);
#endif
