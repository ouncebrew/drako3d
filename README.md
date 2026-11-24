# drako3d

drako3d is a (semi-)brand new homebrew low level 3D graphics API targetting the Nvidia Tegra T239 processor found inside the Nintendo Switch 2. It is based on [deko3d](https://github.com/devkitPro/deko3d). drako3d provides the following features:

- Low level and explicit API reminiscent of Vulkan®: drako3d is modeled after popular low level concepts such as devices, queues, command buffers, and much more.
- Low overhead and explicit resource management: drako3d lets developers take control of resource management, and make decisions that make most sense in each case. In addition, care was taken to avoid unnecessary dynamic memory allocations and other undesired behavior that can result in degraded application performance.
- Control over command generation: drako3d grants developers the ability to manage and record command lists that can be submitted to the GPU, as well as the ability to maintain multiple parallel queues of command processing, each with its own internal (GPU-managed) state.
- Ease of use and convenience: despite being a low level API, care was taken to avoid introducing arbitrary restrictions in the API (such as giant immutable state objects or overengineered abstractions) that do not correspond to how the hardware works. drako3d aims to expose every useful thing that the GPU can do, in the most straightforward way possible. Between pedanticness and programmer happiness, the latter was selected.
- Compile-time resource management: drako3d provides and promotes a workflow involving the compilation and conversion of assets such as shaders or graphics at compile time, therefore avoiding runtime resource waste.
- Debug version of the library, providing parameter and state checking, as well as warning/error messages. The release version of the library omits validation checks and is compiled with extra optimization flags for maximum speed.
- Ability to use a plain C version of the API, as well as a rich C++ wrapper inspired by Vulkan-Hpp.

In conclusion, drako3d is the homebrew answer to Nintendo and Nvidia's proprietary NVN2 graphics API. The source code of drako3d is intended to also serve as a public example of how to use an Nvidia GPU, as well as aiming to be an accurate-as-possible public repository of register definitions and other hardware information.

## Current status

drako3d is currently wip, but it should support everything deko3d does on Switch 1. The public API (`deko3d.h` / `deko3d.hpp`, `Dk*` / `dk::` names) is unchanged from deko3d, and the library can still be built for the original Switch (GM20B) with `GPU_ARCH=maxwell`, however, this is a first and foremost Switch 2 project. If you are exclusively targeting Switch 1 then you should really just use deko3d.

### Working

- Devices, memory blocks, queues, device info
- Command buffers, fences, cross-queue waits, `waitIdle`, command list capture/replay, `callList`, `pushData`
- RGBA8, BGRA8, RGBX8 and RGB10A2 formats, crop rectangles, swap intervals, recreating a swapchain, GPU-side acquire/present synchronization, external fences
- 3D draws, indexed/instanced/indirect draws, vertex buffers and attributes, uniform buffers and push constants, storage buffers, textures and samplers, image load/store/atomics, shader scratch memory
- Tessellation and geometry shaders
- Depth/stencil, depth bounds, blending, multiple render targets, clears
- Culling, polygon modes, line/point size, line and polygon stipple, provoking vertex, scissors, viewports and swizzles, depth clamp and bias
- MSAA (2x/4x/8x), sample masks and locations, alpha to coverage, coverage modulation, resolves
- Compute dispatches
- Transfers
- Report counters
- Zcull
- Colour/depth compression
- GPU error reporting

## Building

Building drako3d requires devkitPro, [drakotools](https://github.com/ouncebrew/drakotools) (`drakodef` and `drakomme`) on your `PATH`, and either libounce (Switch 2) or libnx (Switch 1).

	make                    # Switch 2 (GA10F)
	make GPU_ARCH=maxwell   # Switch 1 (GM20B)
	make install

Run `make clean` when switching `GPU_ARCH`.

## Preemptively Answered Questions (PAQ)

### Can I use the shader compiler inside my program?

The short answer is no (at least not yet): drako3d's shader compiler (UAM2) is a PC tool that runs at compile time as opposed to at runtime, and this is a *main design goal* of drako3d.

However, it is more than likely once more has been finalised with UAM2, that some sort of runtime compiler emerges from it. Despite this, if possible, you really should pre-compile shaders when using drako3d.

### Is this a clone of NVN2?

drako3d is not a clone of NVN2 just as deko3d wasn't a clone of NVN. However, drako3d was developed as a result of NVN2 reverse engineering work, and as such it borrows many implementation ideas. Essentially, drako3d was designed to fulfill the same place in homebrew that NVN does in official software.

### Why and when should I use drako3d instead of kaiHEN's OpenGL/Vulkan drivers?

drako3d is a lightweight low level API; several orders of magnitude more lightweight than any Mesa port. A typical application using Mesa will usually be several megabytes in size (around 20MB or larger); while a typical application using drako3d will clock at around 500 KB or even smaller. The reason for this stems from the fact that mesa is designed in an environment where code size is not an issue, and shared libraries are used to load graphics drivers, which is the opposite situation that exists on Switch. drako3d also is optimised directly for the GPU in the Switch 2 (GA10F)

On the other hand, Mesa provides a standard OpenGL/Vulkan API, while drako3d is a Switch(2) homebrew-specific API. Users would be advised to stick to Mesa (or libraries using it such as SDL) if a standard API is desired for cross-platform support.

### Is drako3d faster than OpenGL/Vulkan?

As mentioned previously, drako3d has native support for several Turing/Ampere/Ada performance-enhancing features that nvk currently hasn't been updated to use. These features were figured out by reverse engineering NVN2, and the results of that work are present in drako3d. In addition, there is substantially lower CPU overhead compared to OpenGL/Vulkan, since drako3d is a low-level graphics API. The same reasons that would prompt users to switch to Vulkan over OpenGL for the purposes of reducing CPU/GPU overhead apply to drako3d as well.

### Why do you call NVN NVN2?

Mostly just to differentiate which iteration of NVN that I'm referring to. Internally, Nintendo calls them both NVN, yet they operate differently depending on the console run (Switch 1 or Switch 2).

## Future plans

- A Switch 2 shader compiler
- FP16/HDR swapchains (requires libounce to support them too)

Most of drako3d's open items still apply as well.

## Credits and acknowledgements

drako3d is presently developed and maintained by **PalindromicBreadLoaf**.<br>
In no particular order, I would like to credit the following people:
- [fincs](https://github.com/fincs) from [devkitPro](https://devkitpro.org/) for creating deko3d, the basis of deko3d. Without him this project wouldn't exist and I wouldn't be where I am.
- [souldbminer](https://github.com/souldbminerr) from [Horizon OC](https://github.com/Horizon-OC) for overall being supportive and getting me further into this whole mess :).
- [WinterMute](https://github.com/WinterMute) from [devkitPro](https://devkitpro.org/) for making the homebrew dream a reality through their incredible toolchains and ecosystem.
- [plutoo](https://github.com/plutooo) from [switchbrew](https://github.com/switchbrew) for kickstarting GPU and NVN reverse engineering efforts.
- [SciresM](https://github.com/SciresM) and [hexkyz](https://github.com/hexkyz) from the [Atmosphère](https://github.com/Atmosphere-NX) project for their invaluable contributions in the reverse engineering of Horizon OS.
- [Rodrigo](https://github.com/ReinUsesLisp) and [Blinkhawk](https://github.com/FernandoS27) from the [yuzu](https://github.com/yuzu-emu) project for tirelessly listening to my rants and assisting me with feedback and GPU knowledge.
- [Thog](https://github.com/Thog) and [gdkchan](https://github.com/gdkchan) from the [Ryujinx](https://github.com/Ryujinx) project for tirelessly listening to my rants and assisting me with feedback and GPU knowledge.

In addition, drako3d would have never been a thing without these organizations:

- [NVIDIA](https://github.com/NVIDIA) for designing and creating the Tegra X1 GPU, as well as the NVN API.
- [Nintendo](https://github.com/Nintendo) for designing and creating Horizon OS.
- [nouveau](https://nouveau.freedesktop.org/wiki/) for creating and maintaining the only open source OpenGL driver for Nvidia hardware.
