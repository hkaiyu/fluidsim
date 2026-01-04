#include "common.slang"

struct VSOut
{
    float4 pos   : SV_Position;
    float3 color : COLOR0;
    float2 ndc   : COLOR1;
};

[[vk::push_constant]]
cbuffer PushConstants
{
    CameraData* cameraData;
    Params* params;
    Particle* particles;
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

    float2 quad = corners[cornerID];

    // world -> view
    float3 viewPos = mul(cd.view, float4(p.pos, 1.0)).xyz;
    float particleRadius = 0.5;
    float3 right = float3(1, 0, 0);
    float3 up = float3(0, 1, 0);
    viewPos += (right * quad.x + up * quad.y) * particleRadius;

    // color with speed
    float speed = length(p.vel);
    float3 blue = float3(.05, 0.05, 0.75);
    float3 green = float3(0.2, 1.0, 0.4);
    // float3 white = float3(1.0);
    float t = saturate(speed * 0.15);

    o.pos = mul(cd.proj, float4(viewPos, 1.0));
    o.ndc = quad;
    o.color = lerp(blue, green, t);
    return o;
}

[shader("fragment")]
float4 FSMain(VSOut i) : SV_Target
{
    float2 p = i.ndc;
    float r2 = dot(p, p);
    if (r2 > 1.0)
        discard;

    // normal
    float z = sqrt(1.0 - r2);
    float3 normal = normalize(float3(p.x, p.y, z));

    // lighting
    float3 lightDir = normalize(params->sunDir);
    float ndotl = saturate(dot(normal, lightDir));
    float3 color = i.color * (0.3 + 0.7 * ndotl);
    return float4(color, 1.0);
}
