# Third-party notices

The source changes apply to [RPCS3](https://github.com/RPCS3/rpcs3) at `ca3e5b268efae904eb9c4c2c97fd62aac980d377`. Its license text is in [LICENSE](LICENSE). The shader interpreter preparation follows the [RPCS3 Vulkan interpreter](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/VK/VKShaderInterpreter.cpp).

The default icon uses the [RPCS3 logo](https://github.com/RPCS3/rpcs3/blob/ca3e5b268efae904eb9c4c2c97fd62aac980d377/rpcs3/rpcs3.svg). The original SVG is included.

| Component | Source and license |
| --- | --- |
| PS5 Vulkan integration | [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan/tree/ace98a99460c95a8d2821a9e33935c7b71070f7f), [GPL version 3 text](licenses/PS5_Vulkan-LICENSE), [upstream notices](licenses/PS5_Vulkan-NOTICE.md) |
| Mesa and RADV | [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa/tree/1ba9874f478657ccf6da72ce55f0260164cc0bd0), [MIT text for RADV and Mesa Vulkan runtime](licenses/Mesa-MIT.txt); individual files retain their upstream terms |
| Vulkan headers | [Khronos Vulkan Headers](https://github.com/KhronosGroup/Vulkan-Headers), [upstream terms](licenses/Vulkan-Headers-LICENSE.md), [Apache 2.0 text](licenses/Mesa-Apache-2.0.txt), [MIT text](licenses/Mesa-MIT.txt) |
| volk | [volk](https://github.com/zeux/volk/tree/aa76c86ae99aa76f7411c981dc885f68a08513b5), [MIT license](licenses/volk-LICENSE.md) |

The LLVM target selector retains [LLVM's Apache 2.0 license with LLVM exceptions](licenses/LLVM-LICENSE.txt). The external SDK, Qt, FFmpeg and the other build inputs retain the terms provided with their own source trees. This repository distributes source changes. Review the complete dependency terms before redistributing linked binaries.
