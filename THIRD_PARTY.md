# Third-party notices

The source changes apply to [RPCS3](https://github.com/RPCS3/rpcs3) at `ca3e5b268efae904eb9c4c2c97fd62aac980d377`. Its license text is in [LICENSE](LICENSE). The shader interpreter preparation follows the [RPCS3 Vulkan interpreter](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/VK/VKShaderInterpreter.cpp).

The Recompiler adapts later RPCS3 changes to [Vulkan pipeline state reuse](https://github.com/RPCS3/rpcs3/commit/63f197d8bb932138712b8fc7c6e740cf52be8f90), [VBlank event delivery](https://github.com/RPCS3/rpcs3/commit/7ef1121871efc4d43ab03d8a44b2f1a596d7407f), [SPU LLVM module initialization](https://github.com/RPCS3/rpcs3/commit/965f4f58933b09dfc0f43d476a88c8161354d158), [MFC DMA lock ownership](https://github.com/RPCS3/rpcs3/commit/c5062063f75f8530c6e6a6c4874a29a249871f6c), [PUTLLUC locks](https://github.com/RPCS3/rpcs3/commit/57ad7e6c7b7db9444b704eec25b658eaaf7ad6f1), [GETLLAR ordering](https://github.com/RPCS3/rpcs3/commit/4017ffc7cf7b6c091535698c7ff5e3a1265f2a82) and [legacy save data callbacks](https://github.com/RPCS3/rpcs3/commit/3bfac607760d39dcdbe77eb137ab6dbb723b3b36).

The default icon uses the [RPCS3 logo](https://github.com/RPCS3/rpcs3/blob/ca3e5b268efae904eb9c4c2c97fd62aac980d377/rpcs3/rpcs3.svg). The original SVG is included.

| Component | Source and license |
| --- | --- |
| PS5 Vulkan integration | [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan/tree/ace98a99460c95a8d2821a9e33935c7b71070f7f), [GPL version 3 text](licenses/PS5_Vulkan-LICENSE), [upstream notices](licenses/PS5_Vulkan-NOTICE.md) |
| Mesa and RADV | [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa/tree/1ba9874f478657ccf6da72ce55f0260164cc0bd0), [MIT text for RADV and Mesa Vulkan runtime](licenses/Mesa-MIT.txt); individual files retain their upstream terms |
| Vulkan headers | [Khronos Vulkan Headers](https://github.com/KhronosGroup/Vulkan-Headers), [upstream terms](licenses/Vulkan-Headers-LICENSE.md), [Apache 2.0 text](licenses/Mesa-Apache-2.0.txt), [MIT text](licenses/Mesa-MIT.txt) |
| volk | [volk](https://github.com/zeux/volk/tree/aa76c86ae99aa76f7411c981dc885f68a08513b5), [MIT license](licenses/volk-LICENSE.md) |

The LLVM target selector retains [LLVM's Apache 2.0 license with LLVM exceptions](licenses/LLVM-LICENSE.txt). The external SDK, Qt, FFmpeg and the other build inputs retain the terms provided with their own source trees. This repository distributes source changes. Review the complete dependency terms before redistributing linked binaries.

The [RADV and VideoOut patch](patches/mesa-videoout.patch) applies to the pinned PS5 Mesa source above and keeps its MIT terms. The native audio backend uses [SoundTouch](https://github.com/RPCS3/soundtouch) through the RPCS3 dependency tree. SoundTouch and its code retain the notices and LGPL terms in that source tree. The SPU hybrid path uses [AsmJit](https://github.com/asmjit/asmjit) through the RPCS3 dependency tree. The asynchronous SPU scheduling was inspired by the [ARMSX3 background compilation work](https://github.com/ARMSX2/ARMSX3/commit/ab79c1ecef2686acd065a59a31fe655e4758ca42) and uses the RPCS3 interpreter and LLVM recompilation paths.
