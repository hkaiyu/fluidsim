#include "common.slang"

[[vk::push_constant]]
cbuffer PushConstants
{
    CameraData* cameraData;
    Particle* particles;
    Params* params;
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 ndc  : TEXCOORD0;
};

[shader("vertex")]
VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;

    CameraData cd = *cameraData;

    uint particleID = vid / 6;
    uint cornerID = vid % 6;

    Particle p = particles[particleID];

    static const float2 corners[6] =
    {
        float2(-1, -1),
        float2( 1, -1),
        float2( 1,  1),
        float2(-1, -1),
        float2( 1,  1),
        float2(-1,  1)
    };

    const float particleRadius = 0.75f;

    float2 quad = corners[cornerID];

    float3 viewPos = mul(cd.view, float4(p.pos, 1.0)).xyz;
    float3 right = float3(1, 0, 0);
    float3 up = float3(0, 1, 0);
    viewPos += (right * quad.x + up * quad.y) * particleRadius;

    o.pos = mul(cd.proj, float4(viewPos, 1.0));
    o.ndc = quad;
    return o;
}

[shader("fragment")]
float FSMain(VSOut i) : SV_Target
{
    // [-1, 1]
    float r2 = dot(i.ndc, i.ndc);
    if (r2 > 1.0) discard;
    return 0.1;
}
