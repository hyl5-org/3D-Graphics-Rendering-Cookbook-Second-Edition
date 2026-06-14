// TODO(luhanyang): Enable this wrapper in a dedicated RTX OMM integration.
//
// #pragma once
//
// #include <stdint.h>
//
// namespace FinalDemo
// {
//
// struct OmmSdkStatus
// {
//     bool compiled = false;
//     bool bakerAvailable = false;
//     bool runtimeBackendReady = false;
//     uint32_t versionMajor = 0;
//     uint32_t versionMinor = 0;
//     uint32_t versionBuild = 0;
// };
//
// struct OmmBakeSettings
// {
//     bool enabled = false;
//     int maxSubdivisionLevel = 5;
//     float dynamicSubdivisionScale = 2.0f;
// };
//
// OmmSdkStatus probeOmmSdk();
// const char *ommSdkStatusText(const OmmSdkStatus &status);
//
// } // namespace FinalDemo
