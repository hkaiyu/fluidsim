// special thanks to https://github.com/SebLague/Fluid-Sim
#include "bindless_config.slang"
#include "common.slang"
#include "full_screen.slang"

[[vk::push_constant]]
cbuffer PushConstant
{
    CameraData *camera;
    Params *params;
    uint2 texResolution;
    uint samplerIdx;
    uint depthIdx;
    uint thicknessIdx;
};

struct LightResponse
{
    float3 reflectDir;
    float3 refractDir;
    float reflectWeight;
    float refractWeight;
};

float2 UVToNDC(float2 uv)
{
    // (0,0) -> (-1,+1)
    float2 ndc;
    ndc.x = uv.x * 2.0 - 1.0;
    ndc.y = 1.0 - uv.y * 2.0;
    return ndc;
}

float3 ViewRayFromScreen(float2 uv)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    float4 clipNear = float4(ndc, 0.0f, 1.0f);
    float4 clipFar  = float4(ndc, 1.0f, 1.0f);

    float4 viewNear = mul(camera->invProj, clipNear);
    float4 viewFar  = mul(camera->invProj, clipFar);

    float3 dirView = normalize((viewFar.xyz / viewFar.w) - (viewNear.xyz / viewNear.w));
    float3 dirWorld = normalize(mul((float3x3)camera->invView, dirView));

    return dirWorld;
}

bool IntersectBox(float3 pos, float3 dir, float3 AA, float3 BB, out float tNear, out float tFar)
{
    const float3 inv = 1.0 / dir;
    float3 t0 = (AA - pos) * inv;
    float3 t1 = (BB - pos) * inv;
    float3 tmin = min(t0, t1);
    float3 tmax = max(t0, t1);
    tNear = max(max(tmin.x, tmin.y), tmin.z);
    tFar =  min(min(tmax.x, tmax.y), tmax.z);
    return tFar > max(tNear, 0.0);
}

float3 SampleSky(float3 dir)
{
    // TODO: different sky color
    // for now, have this to easily visualize reflectance
    return dir;
}

float3 SampleEnvironment(float3 pos, float3 dir)
{
    const float fHeight = 1.0;
    const float fWidth = 300.0;
    const float fDepth = 300.0;
    const float3 centerXZ = float3(params->bounds) * float3(0.5, 0.0, 0.5);
    const float3 offset = float3(fWidth, fHeight, fDepth) * 0.5f;
    const float3 floorAA = centerXZ - float3(offset.x, fHeight, offset.z);
    const float3 floorBB = centerXZ + float3(offset.x, 0.0f, offset.z);

    float tNear, tFar;
    if (IntersectBox(pos, dir, floorAA, floorBB, tNear, tFar))
    {
        // hit floor
        float3 hit = pos + dir * tNear;
        float scale = 0.1;
        int3 c = int3(floor(hit * scale));
        return ((c.x + c.z) & 1) == 0 ? float3(0.2, 0.2, 0.2) : float3(0.8, 0.8, 0.8);
    }
    return SampleSky(dir);
}

float SampleThickness(float2 uv)
{
    SamplerState s = BINDLESS(samplerIdx, SamplerState);
    Texture2D<float> thicknessTex = BINDLESS(thicknessIdx, Texture2D<float>);
    float thickness = thicknessTex.Sample(s, uv) * params->densityMultiplier;
    return pow(saturate(thickness), 0.5);
}

float CalculateReflectance(float3 I, float3 N, float iorA, float iorB)
{
    float cosI = saturate(dot(-I, N)); // incident angle
    float R = pow((iorA - iorB) / (iorA + iorB), 2.0);
    return R + (1.0 - R) * pow(1.0 - cosI, 5.0); // sclick approximation
}

LightResponse CalculateReflectionAndRefraction(float3 inDir, float3 normal, float iorA, float iorB)
{
    LightResponse result;
    result.reflectWeight = CalculateReflectance(inDir, normal, iorA, iorB);
    result.refractWeight = 1 - result.reflectWeight;
    result.reflectDir = reflect(inDir, normal);
    result.refractDir = refract(inDir, normal, iorA / iorB);
    return result;
}

[shader("fragment")]
float4 RenderFluid(VSOut i) : SV_TARGET
{
    CameraData* cam = camera;
    Params* p = params;
    SamplerState s = BINDLESS(samplerIdx, SamplerState);
    Texture2D<float> depthTex = BINDLESS(depthIdx, Texture2D<float>);
    Texture2D<float> thicknessTex = BINDLESS(thicknessIdx, Texture2D<float>);

    float2 uv = i.ndc * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;

    float depth = depthTex.Sample(s, uv);
    float thickness = SampleThickness(uv);

    float3 pos = camera->cameraPos.xyz;
    float3 viewRay = ViewRayFromScreen(uv);

    // no fluid in this pixel
    if (depth <= 0.0 || thickness <= 1e-6) return float4(SampleEnvironment(pos, viewRay), 1.0);

    // miss simulation bounds
    float tNear, tFar;
    if (!IntersectBox(pos, viewRay, 0.0, p->bounds, tNear, tFar)) return float4(SampleEnvironment(pos, viewRay), 1.0);

    // box behind camera
    if (tFar <= 0.0) return float4(SampleEnvironment(pos, viewRay), 1.0);

    // otherwise, fluid has been hit

    // normal calculation
    // TODO: normal approximation could be improved...
    float2 texel = 1.0f / float2(texResolution);
    float depthRight = depthTex.Sample(s, uv + float2(texel.x, 0));
    float depthUp = depthTex.Sample(s, uv + float2(0, texel.y));
    float3 rayRight = ViewRayFromScreen(uv + float2(texel.x, 0));
    float3 rayUp = ViewRayFromScreen(uv + float2(0, texel.y));
    float3 hitDepth = depth * viewRay;
    float3 dx = depthRight * rayRight;
    float3 dy = depthUp * rayUp;
    float3 N = normalize(cross(dx - hitDepth, dy - hitDepth));

    // smoothen thickness
    float tSum = thickness +
        SampleThickness(uv + 5.0 * float2(texel.x, 0)) +
        SampleThickness(uv - 5.0 * float2(texel.x, 0)) +
        SampleThickness(uv + 5.0 * float2(0, texel.y)) +
        SampleThickness(uv - 5.0 * float2(0, texel.y));
    float smoothThickness = tSum / 5.0;

    // calculate reflection and refraction
    LightResponse lightResponse = CalculateReflectionAndRefraction(viewRay, N, 1, 1.33);
    float3 reflectDir = reflect(viewRay, N);

    float3 hitPos = pos + hitDepth;
    float3 exitPos = hitPos + lightResponse.refractDir * smoothThickness * p->refractMultiplier;

    float3 transmission = exp(-smoothThickness * p->extinctionCoeff);
    float3 reflectCol = SampleEnvironment(hitPos, lightResponse.reflectDir);
    float3 refractCol = SampleEnvironment(exitPos, lightResponse.refractDir);
    refractCol *= transmission;

    float3 color = lerp(reflectCol, refractCol, lightResponse.refractWeight);
    return float4(color, 1.0);
}
