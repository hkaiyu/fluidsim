// MLS-MPM paper here: https://yuanming.taichi.graphics/publication/2018-mlsmpm/mls-mpm-cpic.pdf
// Code is adapted from this blog (https://nialltl.neocities.org/articles/mpm_guide) to run on GPU

#include "bindless_config.slang"
#include "common.slang"

static const uint GROUPSIZE = 512;

// ===== PUSH CONSTANT LAYOUTS =====
struct Init_PC
{
    int3 spawnAA;
    uint nparticles;
    int3 spawnBB;
    float spacing;

    Particle* particles;
};

struct Sim_PC
{
    Particle* particles;
    Cell* grid;
    Params* params;
    uint nparticles; float dt;
    uint gravityMode;
};

// ===== HELPERS =====
void QuadraticWeights(float3 d, out float3[3] w)
{
    // quadratic b-spline is calculated in grid space
    float3 h  = float3(0.5);
    float3 t0 = h - d;
    float3 t1 = d;
    float3 t2 = h + d;
    w[0] = 0.5 * t0 * t0;
    w[1] = float3(0.75) - t1 * t1;
    w[2] = 0.5 * t2 * t2;
}

uint GridCell2PaddedGridIdx(int3 cell, uint pad, uint3 gridResNoPad)
{
    uint3 pcell = uint3(cell + (int)pad);
    uint3 gridSize = gridResNoPad + (2 * pad);
    return (pcell.x * gridSize.y + pcell.y) * gridSize.z + pcell.z;
}

float3 ApplySlipWallsPadded(uint3 gridCell, uint pad, uint3 gridResNoPad, float3 v)
{
    float3 nv = v;

    uint xp = gridCell.x;
    uint yp = gridCell.y;
    uint zp = gridCell.z;

    if (xp < pad || xp >= pad + gridResNoPad.x) nv.x = 0.0;
    if (yp < pad || yp >= pad + gridResNoPad.y) nv.y = 0.0;
    if (zp < pad || zp >= pad + gridResNoPad.z) nv.z = 0.0;
    return nv;
}


// ===== KERNELS =====
[shader("compute")]
[numthreads(GROUPSIZE, 1, 1)]
void InitParticles(uint3 id : SV_DispatchThreadID, uniform Init_PC pc)
{
    uint i = id.x;
    if (i >= pc.nparticles) return;

    float3 AA = float3(pc.spawnAA);
    float3 BB = float3(pc.spawnBB);
    float spacing = pc.spacing;

    int3 extent = int3(pc.spawnBB - pc.spawnAA);
    int3 counts = int3(floor(float3(extent) / spacing));

    uint cx = i % counts.x;
    uint cy = (i / counts.x) % counts.y;
    uint cz = i / (counts.x * counts.y);

    float3 pos = AA + float3(cx, cy, cz) * spacing;
    pos = clamp(pos, AA, BB - spacing);

    Particle p;
    p.pos = pos;
    p.vel = float3(0.0, -5.0, 0.0);
    p.mass = 1.0;
    p.C0 = p.C1 = p.C2 = float3(0,0,0);

    p._pad0 = 0.0;
    p._pad1 = 0.0;
    p._pad2 = 0.0;
    p._pad3 = 0.0;

    pc.particles[i] = p;
}

[shader("compute")]
[numthreads(GROUPSIZE,1,1)]
void ClearGrid(uint3 tid : SV_DispatchThreadID, uniform Sim_PC pc)
{
    uint i = tid.x;
    Params* params = pc.params;
    if (i >= params->ncells) return;

    Cell* grid = pc.grid;
    grid[i].pv = float3(0.0);
    grid[i].mass = 0.0;
}

[shader("compute")]
[numthreads(GROUPSIZE,1,1)]
void ScatterMomentum(uint3 tid : SV_DispatchThreadID, uniform Sim_PC pc)
{
    uint pi = tid.x;
    if (pi >= pc.nparticles) return;

    Particle* particles = pc.particles;
    Cell* grid = pc.grid;
    Params* par = pc.params;

    Particle p = particles[pi];

    int3 base = int3(floor(p.pos));
    float3 gridDist = p.pos - float3(base) - 0.5;

    float3 w[3];
    QuadraticWeights(gridDist, w);

    float3x3 C = p.GetAffineMatrix();
    for (int gx=0; gx<3; ++gx)
    for (int gy=0; gy<3; ++gy)
    for (int gz=0; gz<3; ++gz)
    {
        float weight = w[gx].x * w[gy].y * w[gz].z;

        int3 cell = base + int3(gx-1, gy-1, gz-1);
        float3 cellCenter = float3(cell) + 0.5;
        float3 dist = cellCenter - p.pos;

        float3 Q = mul(C, dist);
        float m = weight * p.mass;
        float3 momentum = m * (p.vel + Q);

        int ci = GridCell2PaddedGridIdx(cell, par->gridPad, par->bounds);
        InterlockedAdd(grid[ci].mass, m);
        InterlockedAdd(grid[ci].pv.x, momentum.x);
        InterlockedAdd(grid[ci].pv.y, momentum.y);
        InterlockedAdd(grid[ci].pv.z, momentum.z);
    }
}

[shader("compute")]
[numthreads(GROUPSIZE,1,1)]
void ScatterForce(uint3 tid : SV_DispatchThreadID, uniform Sim_PC pc)
{
    uint pi = tid.x;
    if (pi >= pc.nparticles) return;

    Particle* particles = pc.particles;
    Cell* grid = pc.grid;
    Params* par = pc.params;

    Particle p = particles[pi];

    int3 base = int3(floor(p.pos));
    float3 gridDist = p.pos - float3(base) - 0.5;

    float3 w[3];
    QuadraticWeights(gridDist, w);

    float3x3 C = p.GetAffineMatrix();

    // estimate density
    float density = 0.0;
    for (int gx=0; gx<3; ++gx)
    for (int gy=0; gy<3; ++gy)
    for (int gz=0; gz<3; ++gz)
    {
        float weight = w[gx].x * w[gy].y * w[gz].z;
        int3 cell = base + int3(gx-1, gy-1, gz-1);
        int ci = GridCell2PaddedGridIdx(cell, par->gridPad, par->bounds);
        density += grid[ci].mass * weight;
    }
    if (density <= 1e-10) return;

    float volume = p.mass / max(density, 1e-6);
    float pressure = max(-0.05,
        par->eosStiffness * (pow(density / par->restDensity, par->eosPower) - 1.0)
    );
    float3x3 stress = -pressure * float3x3(1,0,0, 0,1,0, 0,0,1);
    C = 0.5 * (C + transpose(C));
    stress += par->dynamicViscosity * C;
    float3x3 term = (-volume * 4.0 * pc.dt) * stress;

    // scatter stress
    for (int gx=0; gx<3; ++gx)
    for (int gy=0; gy<3; ++gy)
    for (int gz=0; gz<3; ++gz)
    {
        float weight = w[gx].x * w[gy].y * w[gz].z;

        int3 cell = base + int3(gx-1, gy-1, gz-1);
        float3 cellCenter = float3(cell) + 0.5;
        float3 dist = cellCenter - p.pos;
        float3 force = weight * mul(term, dist);

        int ci = GridCell2PaddedGridIdx(cell, par->gridPad, par->bounds);
        InterlockedAdd(grid[ci].pv.x, force.x);
        InterlockedAdd(grid[ci].pv.y, force.y);
        InterlockedAdd(grid[ci].pv.z, force.z);
    }
}

[shader("compute")]
[numthreads(GROUPSIZE,1,1)]
void UpdateGrid(uint3 tid : SV_DispatchThreadID, uniform Sim_PC pc)
{
    uint i = tid.x;
    Params* par = pc.params;
    if (i >= par->ncells) return;

    Cell* grid = pc.grid;
    int pad = (int)par->gridPad;
    int3 gridRes = par->bounds;
    int3 gridSize = gridRes + (2 * pad);

    int xp = (int)(i / (gridSize.y * gridSize.z));
    uint rem = i - (uint)xp * (uint)(gridSize.y * gridSize.z);
    int yp = (int)(rem / (uint)gridSize.z);
    int zp = (int)(rem - (uint)yp * (uint)gridSize.z);

    float3 worldPos = float3(xp, yp, zp) - (float) pad;
    float3 center = float3(gridRes) * 0.5;
    float3 dir = center - worldPos;
    float dist = length(dir);
    if (dist > 1e-5) dir /= dist;

    Cell c = grid[i];
    float mass = c.mass;
    float3 vel = float3(0.0);
    if (mass > 0.0)
    {
        c.pv /= mass; // momentum -> vel
        vel = c.pv;
        switch (pc.gravityMode)
        {
            case 1: // swirl
            {
                float3 up = float3(0, 1, 0);
                float3 swirl = cross(dir, up);
                float sdist = length(swirl);
                if (sdist > 1e-5) swirl /= sdist;

                float k = 10.0;
                float3 spiral = dir + k * swirl;
                float pdist = length(spiral);
                if (pdist > 1e-5) spiral /= pdist;

                c.pv += pc.dt * (spiral * par->gravity);
                break;
            }
            case 2: // center
            {
                c.pv += pc.dt * (dir * par->gravity);
                break;
            }
            default: // normal
            {
                c.pv += pc.dt * float3(0, -par->gravity, 0);
                break;
            }
        }
        c.pv = ApplySlipWallsPadded(uint3(xp, yp, zp), pad, par->bounds, c.pv);
    }
    grid[i] = c;
}

[shader("compute")]
[numthreads(GROUPSIZE,1,1)]
void UpdateParticles(uint3 tid : SV_DispatchThreadID, uniform Sim_PC pc)
{
    uint pi = tid.x;
    if (pi >= pc.nparticles) return;

    Particle* particles = pc.particles;
    Cell* grid = pc.grid;
    Params* par = pc.params;

    Particle p = particles[pi];

    int3 base = int3(floor(p.pos));
    float3 gridDist = p.pos - float3(base) - 0.5;

    float3 w[3];
    QuadraticWeights(gridDist, w);

    float3 v_new = 0.0;
    float3x3 B = float3x3(0);
    for (int gx=0; gx<3; gx++)
    for (int gy=0; gy<3; gy++)
    for (int gz=0; gz<3; gz++)
    {
        float weight = w[gx].x * w[gy].y * w[gz].z;

        int3 cell = base + int3(gx-1, gy-1, gz-1);
        float3 cellCenter = (float3(cell) + 0.5);
        float3 dist = cellCenter - p.pos;

        int ci = GridCell2PaddedGridIdx(cell, par->gridPad, par->bounds);
        float3 gridVel = grid[ci].pv;

        float3 vw = gridVel * weight;
        v_new += vw;

        B += float3x3(vw * dist.x, vw * dist.y, vw * dist.z);
    }
    p.vel = v_new;

    p.SetAffineMatrix(B * 4.0);
    p.pos += p.vel * pc.dt;

    float3 posMin = float3(0.0);
    float3 posMax = float3(par->bounds);
    p.pos = clamp(p.pos, posMin, posMax);

    float3 x_n = p.pos + p.vel;

    if (x_n.x < posMin.x) p.vel.x += (posMin.x - x_n.x);
    if (x_n.x > posMax.x) p.vel.x += (posMax.x - x_n.x);

    if (x_n.y < posMin.y) p.vel.y += (posMin.y - x_n.y);
    if (x_n.y > posMax.y) p.vel.y += (posMax.y - x_n.y);

    if (x_n.z < posMin.z) p.vel.z += (posMin.z - x_n.z);
    if (x_n.z > posMax.z) p.vel.z += (posMax.z - x_n.z);

    particles[pi] = p;
}



