#include "DemoUI.h"

namespace FinalDemo
{

namespace
{

void drawToneMappingCurve(uint32_t width, uint32_t height, const HDRPushConstants &pcHDR)
{
    if (!gSettings.hdr.drawCurves)
    {
        return;
    }

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav;
    ImGui::SetNextWindowBgAlpha(0.8f);
    ImGui::SetNextWindowPos({width * 0.6f, height * 0.7f}, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize({width * 0.4f, height * 0.3f});
    ImGui::Begin("Tone mapping curve", nullptr, flags);
    const int kNumGraphPoints = 1001;
    float xs[kNumGraphPoints];
    float ysUchimura[kNumGraphPoints];
    float ysReinhard2[kNumGraphPoints];
    float ysKhronosPBR[kNumGraphPoints];
    for (int i = 0; i != kNumGraphPoints; i++)
    {
        xs[i] = float(i) / kNumGraphPoints;
        ysUchimura[i] = uchimura(xs[i], pcHDR.P, pcHDR.a, pcHDR.m, pcHDR.l, pcHDR.c, pcHDR.b);
        ysReinhard2[i] = reinhard2(xs[i], pcHDR.maxWhite);
        ysKhronosPBR[i] = PBRNeutralToneMapping(xs[i], pcHDR.startCompression, pcHDR.desaturation);
    }
    if (ImPlot::BeginPlot("Tone mapping curves", {width * 0.4f, height * 0.3f}, ImPlotFlags_NoInputs))
    {
        ImPlot::SetupAxes("Input", "Output");
        ImPlot::PlotLine("Uchimura", xs, ysUchimura, kNumGraphPoints);
        ImPlot::PlotLine("Reinhard", xs, ysReinhard2, kNumGraphPoints);
        ImPlot::PlotLine("Khronos PBR", xs, ysKhronosPBR, kNumGraphPoints);
        ImPlot::EndPlot();
    }
    ImGui::End();
}

} // namespace

void drawControls(uint32_t width, uint32_t height, float aspectRatio, SceneCulling &culling, 
                  HDRPass &hdr, VulkanApp &app)
{
    const ImGuiViewport *v = ImGui::GetMainViewport();
    const float windowWidth = v->WorkSize.x / 5;
    const float indentSize = 16.0f;

    ImGui::SetNextWindowPos(ImVec2(10, 50));
    ImGui::SetNextWindowSizeConstraints(ImVec2(-1, 0), ImVec2(-1, v->WorkSize.y - 210));
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("Draw:");
    ImGui::Indent(indentSize);
    ImGui::Checkbox("Opaque meshes", &gSettings.draw.meshesOpaque);
    ImGui::Checkbox("Bounding boxes", &gSettings.draw.boxes);
    ImGui::Checkbox("Light frustum", &gSettings.draw.lightFrustum);
    ImGui::Unindent(indentSize);
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Frustum Culling"))
    {
        ImGui::Indent(indentSize);
        ImGui::RadioButton("None (N)", &gSettings.culling.mode, CullingMode_None);
        ImGui::RadioButton("CPU  (C)", &gSettings.culling.mode, CullingMode_CPU);
        ImGui::RadioButton("GPU  (G)", &gSettings.culling.mode, CullingMode_GPU);
        ImGui::Unindent(indentSize);
        ImGui::Checkbox("Freeze culling frustum (P)", &gSettings.culling.freezeView);
        ImGui::Separator();
        ImGui::Text("Visible meshes: %i", culling.numVisibleMeshes);
        ImGui::Text("Visible triangles: %llu", (unsigned long long)culling.numVisibleTriangles);
        ImGui::Separator();
    }

    // if (ImGui::CollapsingHeader("Ray Tracing"))
    // {
    //     // TODO(luhanyang): Re-enable after the OMM runtime backend can build and attach micromaps.
    //     // static const OmmSdkStatus ommStatus = probeOmmSdk();
    //     ImGui::Indent(indentSize);
    //     ImGui::Checkbox("RT shadows", &gSettings.rayTracing.shadows);
    //     ImGui::SliderFloat("RT shadow strength", &gSettings.rayTracing.shadowStrength, 0.0f, 1.0f);
    //     ImGui::SliderFloat("RT shadow radius", &gSettings.rayTracing.shadowRadius, 0.0f, 0.1f);
    //     ImGui::RadioButton("Gaussian denoise", &gSettings.rayTracing.denoiseMode, RTDenoiseMode_Gaussian);
    //     ImGui::RadioButton("A-trous denoise", &gSettings.rayTracing.denoiseMode, RTDenoiseMode_Atrous);
    //     ImGui::Checkbox("RTAO", &gSettings.rayTracing.ao);
    //     ImGui::SliderInt("RTAO samples", &gSettings.rayTracing.aoSamples, 1, 16);
    //     ImGui::SliderFloat("RTAO radius", &gSettings.rayTracing.aoRadius, 0.01f, 10.0f);
    //     ImGui::SliderFloat("RTAO power", &gSettings.rayTracing.aoPower, 0.1f, 4.0f);
    //     // ImGui::Separator();
    //     // ImGui::Text("RTX OMM SDK: %s", ommSdkStatusText(ommStatus));
    //     // if (ommStatus.compiled)
    //     // {
    //     //     ImGui::Text("OMM version: %u.%u.%u", ommStatus.versionMajor, ommStatus.versionMinor,
    //     //                 ommStatus.versionBuild);
    //     // }
    //     // ImGui::BeginDisabled(!ommStatus.runtimeBackendReady);
    //     // ImGui::Checkbox("RTX OMM alpha masks", &gSettings.rayTracing.omm.enabled);
    //     // ImGui::SliderInt("OMM max subdivision", &gSettings.rayTracing.omm.maxSubdivisionLevel, 0, 12);
    //     // ImGui::SliderFloat("OMM dynamic scale", &gSettings.rayTracing.omm.dynamicSubdivisionScale, 0.0f, 16.0f);
    //     // ImGui::EndDisabled();
    //     ImGui::Unindent(indentSize);
    //     ImGui::Separator();
    // }

    if (ImGui::CollapsingHeader("Tone Mapping and HDR"))
    {
        ImGui::Indent(indentSize);
        ImGui::Checkbox("Draw tone mapping curves", &gSettings.hdr.drawCurves);
        ImGui::SliderFloat("Exposure", &hdr.pc.exposure, 0.1f, 2.0f);
        ImGui::SliderFloat("Adaptation speed", &gSettings.hdr.adaptationSpeed, 1.0f, 10.0f);
        ImGui::Checkbox("Enable bloom", &gSettings.hdr.enableBloom);
        hdr.pc.bloomStrength = gSettings.hdr.enableBloom ? gSettings.hdr.bloomStrength : 0.0f;
        ImGui::BeginDisabled(!gSettings.hdr.enableBloom);
        ImGui::Indent(indentSize);
        ImGui::SliderFloat("Bloom strength", &gSettings.hdr.bloomStrength, 0.0f, 1.0f);
        ImGui::SliderInt("Bloom num passes", &gSettings.hdr.numBloomPasses, 1, 5);
        ImGui::Unindent(indentSize);
        ImGui::EndDisabled();
        ImGui::Text("Tone mapping mode:");
        ImGui::RadioButton("None", &hdr.pc.drawMode, ToneMapping_None);
        ImGui::RadioButton("Reinhard", &hdr.pc.drawMode, ToneMapping_Reinhard);
        if (hdr.pc.drawMode == ToneMapping_Reinhard)
        {
            ImGui::Indent(indentSize);
            ImGui::BeginDisabled(hdr.pc.drawMode != ToneMapping_Reinhard);
            ImGui::SliderFloat("Max white", &hdr.pc.maxWhite, 0.5f, 2.0f);
            ImGui::EndDisabled();
            ImGui::Unindent(indentSize);
        }
        ImGui::RadioButton("Uchimura", &hdr.pc.drawMode, ToneMapping_Uchimura);
        if (hdr.pc.drawMode == ToneMapping_Uchimura)
        {
            ImGui::Indent(indentSize);
            ImGui::BeginDisabled(hdr.pc.drawMode != ToneMapping_Uchimura);
            ImGui::SliderFloat("Max brightness", &hdr.pc.P, 1.0f, 2.0f);
            ImGui::SliderFloat("Contrast", &hdr.pc.a, 0.0f, 5.0f);
            ImGui::SliderFloat("Linear section start", &hdr.pc.m, 0.0f, 1.0f);
            ImGui::SliderFloat("Linear section length", &hdr.pc.l, 0.0f, 1.0f);
            ImGui::SliderFloat("Black tightness", &hdr.pc.c, 1.0f, 3.0f);
            ImGui::SliderFloat("Pedestal", &hdr.pc.b, 0.0f, 1.0f);
            ImGui::EndDisabled();
            ImGui::Unindent(indentSize);
        }
        ImGui::RadioButton("Khronos PBR Neutral", &hdr.pc.drawMode, ToneMapping_KhronosPBR);
        if (hdr.pc.drawMode == ToneMapping_KhronosPBR)
        {
            ImGui::Indent(indentSize);
            ImGui::SliderFloat("Highlight compression start", &hdr.pc.startCompression, 0.0f, 1.0f);
            ImGui::SliderFloat("Desaturation speed", &hdr.pc.desaturation, 0.0f, 1.0f);
            ImGui::Unindent(indentSize);
        }
        ImGui::Separator();

        ImGui::Text("Average luminance 1x1:");
        ImGui::Image(hdr.pc.texLuminance, ImVec2(128, 128));
        ImGui::Separator();
        ImGui::Text("Bright pass:");
        ImGui::Image(hdr.brightPass.index(), ImVec2(windowWidth, windowWidth / aspectRatio));
        ImGui::Text("Bloom pass:");
        ImGui::Image(hdr.bloomPass.index(), ImVec2(windowWidth, windowWidth / aspectRatio));
        ImGui::Separator();
        ImGui::Text("Luminance pyramid 512x512");
        for (uint32_t l = 0; l != hdr.luminanceViews.size(); l++)
        {
            ImGui::Image(hdr.luminanceViews[l].index(), ImVec2((int)windowWidth >> l, ((int)windowWidth >> l)));
        }
        ImGui::Unindent(indentSize);
        ImGui::Separator();
    }

    if (ImGui::CollapsingHeader("Camera Presets"))
    {
        const vec3 up(0.0f, 1.0f, 0.0f);
        if (ImGui::Button("Street Overview"))
        {
            app.positioner_.lookAt(vec3(-18.6f, 4.6f, -6.4f), vec3(0.0f, 5.0f, 0.0f), up);
        }
        if (ImGui::Button("Cafe Entrance"))
        {
            app.positioner_.lookAt(vec3(-4.0f, 2.5f, -2.0f), vec3(-8.0f, 2.0f, -2.0f), up);
        }
        if (ImGui::Button("Archway"))
        {
            app.positioner_.lookAt(vec3(-10.0f, 3.0f, 1.5f), vec3(-6.0f, 2.5f, 0.0f), up);
        }
        if (ImGui::Button("Low Angle"))
        {
            app.positioner_.lookAt(vec3(-12.0f, 1.5f, -3.0f), vec3(-5.0f, 4.0f, 0.0f), up);
        }
        if (ImGui::Button("Bird's Eye"))
        {
            app.positioner_.lookAt(vec3(-25.0f, 12.0f, -8.0f), vec3(0.0f, 0.0f, 0.0f), up);
        }
        ImGui::Separator();
    }

    ImGui::End();

    drawToneMappingCurve(width, height, hdr.pc);
}

} // namespace FinalDemo
