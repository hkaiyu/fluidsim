struct Particle
{
    float3 C0; float _pad0;
    float3 C1; float _pad1;
    float3 C2; float _pad2;
    float3 pos; float mass; // pos is sim space in [0.0, BB)
    float3 vel; float _pad3;

    float3x3 GetAffineMatrix()
    {
        return float3x3(C0, C1, C2);
    }

    [mutating]
    void SetAffineMatrix(float3x3 C)
    {
        C0 = C[0];
        C1 = C[1];
        C2 = C[2];
    }
};

struct Cell
{
    float3 pv;   // momentum during P2G -> velocity after UpdateGrid
    float  mass;
};

struct Params
{
    uint3   bounds;
    uint    gridPad;
    float3  invBounds;
    uint    _pad0;
    uint3   densityRes;
    uint    _pad1;
    float3  invDensityRes;
    uint    ncells;

    float gravity;
    float restDensity;
    float dynamicViscosity;
    float eosStiffness;
    float eosPower;
    uint  _pad20;
    uint  _pad21;
    uint  _pad22;

    float3 extinctionCoeff;
    float densityThreshold;
    float3 sunDir;
    float densityMultiplier;
    float shadowMultiplier;
    float shadowThreshold;
    float stepSize;
    float refractMultiplier;
};

struct CameraData
{
    float4x4 view;
    float4x4 proj;
    float4x4 invView;
    float4x4 invProj;
    float4   cameraPos;
    float4   cameraDir;
};

