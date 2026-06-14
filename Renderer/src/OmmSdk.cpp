// TODO(luhanyang): Enable this wrapper in a dedicated RTX OMM integration.
//
// #include "OmmSdk.h"
//
// #if defined(COOKBOOK_WITH_RTX_OMM)
// #include <omm.h>
// #endif
//
// namespace FinalDemo
// {
//
// OmmSdkStatus probeOmmSdk()
// {
//     OmmSdkStatus status;
//
// #if defined(COOKBOOK_WITH_RTX_OMM)
//     status.compiled = true;
//     status.versionMajor = OMM_VERSION_MAJOR;
//     status.versionMinor = OMM_VERSION_MINOR;
//     status.versionBuild = OMM_VERSION_BUILD;
//
//     ommBakerCreationDesc desc = ommBakerCreationDescDefault();
//     desc.type = ommBakerType_CPU;
//
//     ommBaker baker = nullptr;
//     status.bakerAvailable = ommCreateBaker(&desc, &baker) == ommResult_SUCCESS && baker != nullptr;
//     if (baker)
//     {
//         ommDestroyBaker(baker);
//     }
//
//     // TODO(luhanyang): Set this once LVK exposes VK_EXT_opacity_micromap objects and BLAS attachment.
//     status.runtimeBackendReady = false;
// #endif
//
//     return status;
// }
//
// const char *ommSdkStatusText(const OmmSdkStatus &status)
// {
//     if (!status.compiled)
//     {
//         return "not built";
//     }
//
//     if (!status.bakerAvailable)
//     {
//         return "baker unavailable";
//     }
//
//     return status.runtimeBackendReady ? "available" : "SDK linked, backend pending";
// }
//
// } // namespace FinalDemo
