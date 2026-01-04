#include "common.slang"

static const float3 offsets[8] = float3[](

    // front face
    float3(0.0f, 0.0f, 0.0f), // bottom left
    float3(1.0f, 0.0f, 0.0f), // bottom right
    float3(0.0f, 1.0f, 0.0f), // top left
    float3(1.0f, 1.0f, 0.0f), // top right

    // back face
    float3(0.0f, 0.0f, 1.0f), // bottom left
    float3(1.0f, 0.0f, 1.0f), // bottom right
    float3(0.0f, 1.0f, 1.0f), // top left
    float3(1.0f, 1.0f, 1.0f)  // top right
);

static const int edges[24] = int[](
    0, 1, // bottom front edge
    1, 3, // right front edge
    3, 2, // top front edge
    2, 0, // left front edge
    0, 4, // bottom left edge
    1, 5, // bottom right edge
    4, 5, // bottom back edge
    5, 7, // right back edge
    7, 6, // top back edge
    6, 4, // left back edge
    6, 2, // top left edge
    7, 3  // top right edge
);

[[vk::push_constant]]
cbuffer PushConstants
{
    CameraData* cameraData;
    Params* params;
};

struct VSOut
{
    float4 pos : SV_Position;
};

[shader("vertex")]
VSOut vsMain(uint vid : SV_VertexID)
{
    CameraData cd = *cameraData;
    VSOut o;
    float3 p = lerp(float3(0.0), float3(params.bounds), offsets[edges[vid]]);

    float4 world = float4(p, 1.0f);
    float4 view = mul(cd.view, world);
    float4 clip = mul(cd.proj, view);
    o.pos = clip;
    return o;
}

[shader("fragment")]
float4 fsMain(VSOut i) : SV_Target
{
    return float4(1.0f, 0.0f, 1.0f, 1.0f);
}

