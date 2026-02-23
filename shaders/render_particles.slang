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
    float4 posWorld : TEXCOORD0;
    float2 uv  : TEXCOORD1;
};

[shader("vertex")]
VSOut vsMain(uint vid : SV_VertexID)
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

    const float particleRadius = 1.0f;

    float2 quad = corners[cornerID];

    float3 viewPos = mul(cd.view, float4(p.pos, 1.0)).xyz;
    float3 right = float3(1, 0, 0);
    float3 up = float3(0, 1, 0);
    viewPos += (right * quad.x + up * quad.y) * particleRadius;

    o.pos = mul(cd.proj, float4(viewPos, 1.0));
    o.uv  = quad * 0.5 + 0.5;
    o.posWorld = float4(p.pos, 1.0);
    o.posWorld.w = particleRadius;
    return o;
}

[shader("fragment")]
float fsMain(VSOut i) : SV_Target
{
    // [0, 1] -> [-1, 1]
    float2 p = i.uv * 2.0 - 1.0;
    float r2 = dot(p, p);
    if (r2 > 1.0) discard;

    CameraData cd = *cameraData;
    float z = sqrt(1 - r2);
    float d = length(i.posWorld.xyz - cd.cameraPos.xyz) - z * i.posWorld.w * 0.1;
    return d;
}
