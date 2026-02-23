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

void GetFloorBounds(out float3 AA, out float3 BB)
{
    const float3 size = float3(300, 1, 300);
    const float2 centerXZ = params->bounds.xz * 0.5;
    const float3 centerFloor = float3(centerXZ.x, -size.y * 0.5, centerXZ.y);
    const float3 offset = size * 0.5;
    AA = centerFloor - offset;
    BB = centerFloor + offset;
}

float3 SampleFloor(float3 pos)
{
    float scale = 0.1;
    int2 c = int2(floor(pos.xz * scale));
    bool checker = ((c.x + c.y) & 1) == 0;
    return checker ? float3(0.1) : float3(0.9);
}

float3 SampleSky(float3 dir)
{
    float t = saturate(dir.y * 0.5f + 0.5f);
    float3 horizon = float3(0.55, 0.75, 1.0); // pale blue near horizon
    float3 zenith  = float3(0.05, 0.25, 0.65); // deep blue overhead
    t = pow(t, 0.8);
    return lerp(horizon, zenith, t);
}

float3 SampleEnvironment(float3 pos, float3 dir)
{
    float3 floorAA, floorBB; GetFloorBounds(floorAA, floorBB);

    float tNear, tFar;
    if (IntersectBox(pos, dir, floorAA, floorBB, tNear, tFar))
    {
        float3 hit = pos + dir * tNear;
        return SampleFloor(hit);
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
    if (depth <= 0.0 || thickness <= 0.1) return float4(SampleEnvironment(pos, viewRay), 1.0);

    float tNearSim, tFarSim;
    bool simHit = IntersectBox(pos, viewRay, 0.0, params->bounds, tNearSim, tFarSim);
    if (!simHit || tFarSim <= 0.0) return float4(SampleEnvironment(pos, viewRay), 1.0);

    // otherwise, fluid has been hit
    float3 hitDepth = depth * viewRay;
    float3 hitPos = pos + hitDepth;

    float tNearFloor, tFarFloor;
    float3 fAA, fBB; GetFloorBounds(fAA, fBB);
    bool floorHit = IntersectBox(pos, viewRay, fAA, fBB, tNearFloor, tFarFloor);
    if (floorHit && tNearFloor < length(hitDepth))
        return float4(SampleFloor(pos + viewRay * tNearFloor), 1.0);

    // normal calculation
    // TODO: normal approximation could be improved...
    float2 texel = 1.0f / float2(texResolution);
    float depthRight = depthTex.Sample(s, uv + float2(texel.x, 0));
    float depthUp = depthTex.Sample(s, uv + float2(0, texel.y));
    float3 rayRight = ViewRayFromScreen(uv + float2(texel.x, 0));
    float3 rayUp = ViewRayFromScreen(uv + float2(0, texel.y));
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

    float3 exitPos = hitPos + lightResponse.refractDir * smoothThickness * p->refractMultiplier;

    float3 transmission = exp(-smoothThickness * p->extinctionCoeff);
    float3 reflectCol = SampleEnvironment(hitPos, lightResponse.reflectDir);
    float3 refractCol = SampleEnvironment(exitPos, lightResponse.refractDir);
    refractCol *= transmission;

    float3 color = lerp(reflectCol, refractCol, lightResponse.refractWeight);
    return float4(color, 1.0);
}
