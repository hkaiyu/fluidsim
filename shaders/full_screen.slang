struct VSOut
{
    float4 pos : SV_Position;
    float2 ndc : TEXCOORD0;
};

[shader("vertex")]
VSOut FullScreenTriangle(uint id : SV_VertexID)
{
    const float2 triVerts[3] = {
        float2(-1,  3),
        float2(-1, -1),
        float2( 3, -1),
    };

    VSOut o;
    o.ndc = triVerts[id];
    o.pos = float4(triVerts[id], 0, 1);
    return o;
}
