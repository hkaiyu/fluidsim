# Vulkan Fluid Simulation

A WIP fluid simulation written using C++ and Vulkan. 

![final](https://github.com/user-attachments/assets/92d385e2-7eaf-4d36-9631-f97ceae3141e)


## Simulation

The simulation itself uses the MLS-MPM algorithm (GitHub link [here](https://github.com/yuanming-hu/taichi_mpm)) and is mainly implemented in compute shaders.
MLS-MPM is a hybrid Eulerian (grid-based) and Lagrangian (particle-based) approach that broadly can be split up into the following stages:

+ Clear grid: We first need to zero out the grid cells.
+ Particle-to-Grid (P2G): After particles are initialized with some state, P2G scatters each particle's data into a grid containing accumulated information about the particles which get mapped to that grid cell.
+ Update grid: After grid is populated with data, we then can calculate the velocity of that grid cell (we can also apply external forces and enforce boundaries here).
+ Grid-to-Particle (G2P): After the grid has updated each cell, we update each particle with the grid cell data.

In MLS-MPM, we don't require expensive neighborhood calculations like Lagragian methods, or require too high of grid resolution like in Eulerian. The main bottleneck MLS-MPM is that we have to scatter many particles' data
to a relatively small number of grid cells. When many particles map to the same grid cell, we have to atomically accumulate data to that cell, which will highly serialize the otherwise highly parallel algorithm.
MLS-MPM is also extremely flexible in the types of particle simulations you can do. This project just does a compressible liquid simulation.

## Rendering

The rendering is heavily inspired by the [screen-space rendering video](https://www.youtube.com/watch?v=kOkfC5fLfgE) made by Sebastian Lague, which is inspired by the following Nvidia 
[slides](https://developer.download.nvidia.com/presentations/2010/gdc/Direct3D_Effects.pdf) by Simon Green on real-time screen space rendering.

<img width="400" height="270" alt="billboard-fluid" src="https://github.com/user-attachments/assets/9bdeeac4-4f1c-459a-b196-7fca6dfd447d" />
<img width="400" height="270" alt="screen-space-fluid" src="https://github.com/user-attachments/assets/ea22daa6-fa1d-4936-9e21-5e5ad1243cb1" />

At a high-level, the rendering is implemented as follow:

+ Calculate 2D depth texture of particles: Particles are rendered as spherical billboards and the depth is written to a depth texture.
+ Calculate 2D thickness of the fluid: Particles are rendered again as spherical billboards, except additive blending is used to approximate the thickness of the fluid.
+ Filtering on depth texture: The depth texture is filtered using vertical and horizontal 1-D Bilateral filtering. This is an approximation of the more accurate 2-D Bilateral filtering even though technically the Bilateral filter is not
separable.
+ Final render: The normals are estimated with the smoothed depth texture, which is used for calculating Fresnel reflection (approximated with Schlick). The transmitted light through the fluid is estimated using the thickness texture and
Beer's law.

## Other Implementation Details

This project is mainly me testing out a bunch of newer Vulkan features such as descriptor buffers, buffer device addresses, and bindless descriptors. I also tried out the Slang shading language for my shaders. I use GLM for math,
ImGui for immediate-mode UI, Vulkan Memory Allocator for GPU allocation, Volk for loading Vulkan functions, and GLFW for window/input handling.

## Building

**Disclaimer: No attempt has yet been made to make this application portable. It is mainly a side project of mine that is for learning. It does not support Linux or MacOS.**

This project uses CMake to build the project. All shaders are pre-compiled into the source code, so no shader compilation is needed. I also vendor most of the needed dependencies, except for GLFW.
CMake fetches GLFW when building.
