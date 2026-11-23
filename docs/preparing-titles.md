# Preparing a title

Use separate working directories for your original PS3 disc dump, private RPCS3 game data, and the PS5 title. Keep your PS3 title ID for guest content and use a distinct PS5 package title ID for every prepared game.

## Disc verification and keys

Start with your own disc or a folder dump you already made. For an encrypted ISO, match the exact disc edition using its title ID, region, languages, and disc revision. Use your own disc key with the compatible disc dumping tools described by RPCS3. An IRD is disc verification metadata. Verify the resulting files against the matching IRD where your dump tool supports it. Verify the extracted `PS3_GAME/PARAM.SFO` title ID before building, and boot the folder in desktop RPCS3 before adding any update or patch. Keep disc keys, IRDs, logs and disc contents in your private workspace. [RPCS3 Quickstart](https://rpcs3.net/quickstart)

```sh
python tools/prepare_dump.py inspect /path/to/verified-disc
```

## PS5 artwork and in-game language

| File | Location | Preparation |
| --- | --- | --- |
| `icon0.png` | `sce_sys/` | 512×512 opaque RGB PNG for custom artwork. `--icon0` fits the complete design into the square and fills the surrounding area with a blurred backdrop, preserving the image proportions. The default uses the RPCS3 icon. |
| `pic0.dds` | `sce_sys/` | 3840×2160 BC7_UNORM DDS, DX10 header, one mip level. Set with `--pic0` for PS5 title artwork. |
| `pic1.dds` | `sce_sys/` | The same DDS format for the PS5 background. Set with `--pic1`. When the same image should appear in both PS5 positions, pass it to both options. |
| `PIC0.PNG` and `PIC0_XX.PNG` | Original `PS3_GAME/` | Keep the default and every localized PNG. The runtime uses this artwork on the compilation screen. Verify which numbered images the disc provides for each supported language. |

Use `localizedParameters` in the target `param.json` for PS5 menu names. Guest language follows the PS5 system language for languages the disc provides. Test the default and one alternate language before packaging. Keep the original disc artwork and IDs in `PS3_GAME`.

## Updates, DLC and patches

1. In desktop RPCS3, install the exact-region PS3 title update into an isolated profile and confirm its `APP_VER`. Pack that clean update tree with `tools/pack_update.py`. The output `UPDATE.BIN` and its generated C++ manifest are a pair. Build the launcher with that manifest and stage the resulting `.BIN` beside its EBOOT. This format is generated from installed files. Renaming a `.pkg` does not create it.
2. Install your DLC in a separate local profile with its valid entitlement where required. Keep the installed DLC files under `dev_hdd0/game/<PS3 title ID>/` and any matching external entitlement under the corresponding private `dev_hdd0/home/<user>/exdata/`. Check it in desktop RPCS3 and use the same private paths on the native runtime. Check that the update is selected and the DLC appears in the game itself.
3. Some games read one fixed DLC filename and expect one pack at a time. For that layout, keep the title's packs separate in your private staging area, inventory each with its SHA-256, and select one verified pack for the exact guest filename the title reads. Keep a copy of the installed pack before switching. Preserve region-specific filenames and verify playback after every switch.
4. Keep RPCS3 patch YAML in the private runtime `patches/` directory and the matching enablement in `patch_config.yml`. Select patches for the updated executable hash, title ID, and game version. Verify them after the `.BIN` update is mounted. A version change can invalidate patch addresses.

The public repository and its releases contain the recompiler sources, tools, and templates. Keep local title payloads, generated manifests, title assets, and licenses outside Git.
