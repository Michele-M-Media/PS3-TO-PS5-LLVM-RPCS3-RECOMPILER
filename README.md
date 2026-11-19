# RPCS3 Recompiler

RPCS3 Recompiler boots a local PS3 disc dump directly with a PS5 build of the RPCS3 runtime. It uses LLVM for PPU and SPU code and the native Vulkan backend for RSX graphics. The profile keeps RPCS3's default GPU settings, including resolution scale, frame limit and VBlank. Vulkan and the PS5 audio backend are selected for the target.

The Vulkan changes include the interpreter's base pipeline catalogue, parallel preparation, shared driver caching, device-checked dynamic states, depth and feedback passes, and retry of pending texture uploads. LLVM and Vulkan caches are built locally and reused on later launches. A new game or driver can still require compilation. Compatibility and timing depend on the game and need to be tested on the target.

## Prepare a disc

Use a directory containing `PS3_DISC.SFB`, `PS3_GAME/PARAM.SFO`, and `PS3_GAME/USRDIR/EBOOT.BIN`. Keep the original title ID and the full disc structure. RPCS3's guide describes supported dump formats and local firmware installation. [RPCS3 Quickstart](https://rpcs3.net/quickstart)

```sh
python tools/prepare_dump.py inspect /path/to/disc-dump
python tools/prepare_dump.py stage \
  --disc /path/to/disc-dump \
  --firmware /path/to/dev_flash \
  --core /path/to/packaged/eboot.bin \
  --param /path/to/homebrew/sce_sys/param.json \
  --output local/DiscA
```

Use a `param.json` from the local package for the target PS5 homebrew title and its matching platform modules. The tool sets the display name to **RPCS3 Recompiler** in each locale and preserves the package identifiers. `templates/param.json` shows the name fields with placeholder IDs. Keep platform modules supplied by the native build in `sce_module/` when needed. Run the resulting directory with your PS5 homebrew environment.

The package uses the RPCS3 icon by default. Pass `--icon0 path/to/icon.png` to replace the icon and `--pic0 path/to/background.png` to add a background. These options only change the local output directory.

## Install an update or DLC

1. In desktop RPCS3, install your update or DLC package for the disc's exact PS3 title ID and region using **File → Install Packages/Raps/Edats**. Install content you own with the associated license if required by RPCS3.
2. Use desktop RPCS3 to check that the installed title works. Keep the disc dump intact. The installed title folder is under `dev_hdd0/game/<PS3 title ID>`. Copy that folder to the corresponding location in the private native data directory, by default `/data/rpcs3-recompiler/dev_hdd0/game/<PS3 title ID>`.
3. If the DLC requires a local entitlement, use the matching private `dev_hdd0/home/<user>/exdata` directory. The default profile uses user `00000001`. Keep the package files, game data and licenses local.
4. Launch the disc. RPCS3 detects an update using the matching title ID and the installed update's `PARAM.SFO` and `USRDIR/EBOOT.BIN`. The guest game looks up DLC in its installed data.

Cache and logs are kept under `/data/rpcs3-recompiler`. Keep the cache between runs to reuse work. The exact shader states, scene data, patches and DLC manifests for individual games belong to local title profiles.

## Build

This repository contains source changes for the native runtime. Build the target dependencies first using their pinned revisions in [graphics-dependencies.json](graphics-dependencies.json). The PS5 configuration expects a native LLVM toolchain, Qt, FFmpeg, Vulkan/RADV, and their PS5 paths. The options and expected paths are recorded in the platform patch and checked by CMake.

For the native LLVM build, use [PS5_LLVM](https://github.com/lavavex/PS5_LLVM/tree/be5b58f51c93c67947b7e18cd45c2f9913f2c311). Copy `llvm-overlay/llvm/lib/ExecutionEngine/TargetSelect.cpp` to the same relative path in that checkout before building its libraries. The launcher and its linked LLVM target selector use the same native tracing symbols.

```sh
git clone https://github.com/RPCS3/rpcs3.git rpcs3
git -C rpcs3 checkout ca3e5b268efae904eb9c4c2c97fd62aac980d377
git -C rpcs3 submodule update --init --recursive
python tools/apply_overlay.py rpcs3
cmake -S rpcs3 -B build/ps5 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/rpcs3/ps5/toolchain.cmake" \
  -DRPCS3_RECOMPILER=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -C /path/to/native-dependencies.cmake
cmake --build build/ps5 --target rpcs3
```

Pass the native ELF to the application packaging tools provided by your target environment. Use the resulting `eboot.bin` with the staging command above.

RPCS3 source keeps its upstream license and notices. Vulkan-related license texts and dependency notices are in [licenses](licenses). See [THIRD_PARTY.md](THIRD_PARTY.md) for the source references.
