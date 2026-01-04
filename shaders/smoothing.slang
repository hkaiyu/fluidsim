#include "bindless_config.slang"

[[vk::push_constant]]
cbuffer PushConstants
{
    uint2 imageResolution;

    // bindless indicies
    uint inTex;
    uint outTex;

    // kernel params
    float rangeSigma;
    float spatialSigma;
    int kernelRadius; // full size is 2 * radius + 1
};

float Gaussian(float x, float sigma)
{
    float invSigma = 1.0 / sigma;
    return invSigma * 0.39894 * exp(-(x * x * 0.5 * invSigma * invSigma));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void BilateralFilter_H(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= imageResolution.x || tid.y >= imageResolution.y) return;

    RWTexture2D<float4> src = BINDLESS(inTex, RWTexture2D<float4>);
    RWTexture2D<float4> dst = BINDLESS(outTex, RWTexture2D<float4>);

    uint2 pix = uint2(tid.xy);
    float p = src[pix].x;

    float sum = 0.0;
    float wsum = 0.0;
    for (int i = -kernelRadius; i <= kernelRadius; i++)
    {
        uint2 nbr = uint2(clamp(int(pix.x) + i, 0, int(imageResolution.x - 1)), pix.y);
        float q = src[nbr].x;

        // spatial domain
        float s = Gaussian(float(i), spatialSigma);

        // range domain
        float r = Gaussian(q - p, rangeSigma);

        sum += q * s * r;
        wsum += s * r;
    }

    float result = wsum > 0.0 ? sum / wsum : sum;
    dst[pix] = float4(result.xxx, 1.0);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void BilateralFilter_V(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= imageResolution.x || tid.y >= imageResolution.y) return;

    RWTexture2D<float4> src = BINDLESS(inTex, RWTexture2D<float4>);
    RWTexture2D<float4> dst = BINDLESS(outTex, RWTexture2D<float4>);

    uint2 pix = uint2(tid.xy);
    float p = src[pix].x;

    float sum = 0.0;
    float wsum = 0.0;
    for (int i = -kernelRadius; i <= kernelRadius; i++)
    {
        uint2 nbr = uint2(pix.x, clamp(int(pix.y) + i, 0, int(imageResolution.y - 1)));
        float q = src[nbr].x;

        // spatial domain
        float s = Gaussian(float(i), spatialSigma);

        // range domain
        float r = Gaussian(q - p, rangeSigma);

        sum += q * s * r;
        wsum += s * r;
    }

    float result = wsum > 0.0 ? sum / wsum : sum;
    dst[pix] = float4(result.xxx, 1.0);
}


