# Third party code

The build needs these, and the installer ships some of them. They are in the
repo so that a clone builds with no more steps. Each keeps its own license.
The full license texts that go out with the mod are in
`bwvr/installer/Notices.txt`.

| Folder | What | Version | License | From |
| --- | --- | --- | --- | --- |
| `minhook/` | MinHook, the hook library (source: `include`, `src`) | commit `8af6b4a` | BSD 2-Clause (`LICENSE.txt`) | https://github.com/TsudaKageyu/minhook |
| `OpenXR-SDK/include/` | The OpenXR headers | release 1.1.43 | Apache-2.0 (`LICENSE`) | https://github.com/KhronosGroup/OpenXR-SDK |
| `OpenXR-SDK/build32/src/loader/Release/` | The 32 bit OpenXR loader, `openxr_loader.dll` and its import library | release 1.1.43 | Apache-2.0 | built from the SDK, see below |
| `backends/dx7.games/` | dxwrapper: `dxwrapper.dll` and its `ddraw.dll` stub. It turns the DirectX 7 of the game into Direct3D 9 | release 1.8.8600.25 | see `bwvr/installer/Notices.txt`, section 4 | https://github.com/elishacloud/dxwrapper |
| `Vulkan-Headers/include/` | The C headers of Vulkan (`vulkan/*.h`, `vk_video/*.h`), no C++ headers | tag `vulkan-sdk-1.4.357.0` | Apache-2.0 or MIT (`LICENSE.md`, `LICENSES/`) | https://github.com/KhronosGroup/Vulkan-Headers |
| `cgltf/` | cgltf, the glTF reader for a hand model file (`cgltf.h`) | v1.14 | MIT (`LICENSE`) | https://github.com/jkuhlmann/cgltf |

## The binaries are pinned

`bwvr/tools/package.ps1` holds the SHA-256 of `openxr_loader.dll`,
`dxwrapper.dll` and the `ddraw.dll` stub, and it stops when a file is not
the recorded one. So a binary here changes only together with its hash in
that script, in one commit that says where the new file is from.

## How the OpenXR loader was built

The game is a 32 bit program, and the SDK ships no 32 bit loader. This one
is a build of the SDK source at `release-1.1.43`:

```
cmake -S OpenXR-SDK -B OpenXR-SDK/build32 -A Win32 -DDYNAMIC_LOADER=ON -DBUILD_TESTS=OFF -DBUILD_API_LAYERS=OFF
cmake --build OpenXR-SDK/build32 --config Release
```

A new build gives a file with a different hash. Only do this to move to a
new SDK version, and update the hash in `package.ps1` in the same commit.

## Not in the repo

`bw1-decomp` (https://github.com/openblack/bw1-decomp) has names for about
26,000 functions of the game. The tools that read the code of the game use
its symbol file. `.\bwvr\tools\re.ps1 names` puts one commit of it in
`third_party/bw1-decomp`. The script holds the commit. That folder is not
tracked.
