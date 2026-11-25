# Building OpenTS for macOS

The macOS port builds `OpenTS.app`, a native arm64 or x86_64 application that
renders through Metal. It needs the original Tiberian Sun data files, the same
ones the Windows build reads.

## Requirements

| Component | Requirement |
| --- | --- |
| macOS | 13.3 (Ventura) or newer |
| Processor | Apple silicon (arm64) or Intel (x86_64 with SSE4.2) |
| Compiler | Xcode command line tools (`xcode-select --install`), AppleClang 15 or newer |
| CMake | 3.23 or newer (`brew install cmake`) |
| Ninja | Optional, faster (`brew install ninja`) |
| Python 3 | The one the command line tools install, used to compile the string table |

## Build

```sh
git clone --recurse-submodules <repository> OpenTS
cd OpenTS
tools/macos/build.sh            # Release; pass Debug for a debug build
tools/macos/build.sh --zip      # also writes build-macos/OpenTS-macOS-<arch>.zip
```

The script configures `build-macos/`, builds, and gives the bundle an ad hoc
signature so it runs on Apple silicon. The app is
`build-macos/bin/OpenTS.app`. The same build without the script:

```sh
cmake -S . -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-macos
codesign --force --deep --sign - build-macos/bin/OpenTS.app
```

## Game data

Without `-DATADIR=`, the app looks for `TIBSUN.MIX` in these folders and uses
the first that has it:

1. the folder that holds `OpenTS.app`;
2. `OpenTS.app/Contents/MacOS`;
3. `~/Library/Application Support/OpenTS/TiberianSun`.

Copy the contents of a Tiberian Sun installation (the Ultimate Collection
release, or the 2010 freeware release) into one of them. On macOS's default
case-insensitive volume the file names need no renaming.

Settings, saved games, screenshots and the single-instance lock go to
`~/Library/Application Support/OpenTS` unless `-USERDIR=` names another folder.
Logs go to the `Debug` folder there, or to `~/Library/Logs/OpenTS` when no user
directory is set.

To pass options, start the executable inside the bundle:

```sh
build-macos/bin/OpenTS.app/Contents/MacOS/OpenTS -DATADIR="$HOME/Games/Tiberian Sun" -XC
```

## How the port works

The engine is shared with the Windows build. The POSIX-specific code is under
`code/platform/posix/`:

| File | Role |
| --- | --- |
| `wincompat.h`, `wincompat.cpp` | The Win32 types and functions the engine calls: files, directory scans, time, text conversion, message boxes, key state. CMake forces the header into every engine source. |
| `windows.h`, `mmsystem.h`, `winsock.h`, ... | One-line stand-ins that resolve the engine's Windows includes to `wincompat.h`. |
| `gdicompat.h`, `gdicompat.cpp` | The GDI subset `DSurface` uses: 16-bit DIB sections, stretched copies, and text drawn with FreeType from `ui/Arimo.ttf`. |
| `langstrings.cpp` | The language strings. `tools/rcstrings.py` compiles the string tables of `code/language/language.rc` into the game, replacing `Language.dll`. |
| `posixpaths.h` | Where the executable, the bundle, the UI files and the user directory are. |
| `macos/` | The bundle's `Info.plist` template and icon. |

Other changes in shared code:

- `long` is replaced by `int32_t` and `unsigned long` by `uint32_t` in the
  engine and the VQA player. Windows' `long` is 32 bits and file formats such
  as VQA chunk headers and the SHA state depend on that; on macOS `long` is 64
  bits. On Windows the two spellings are the same type width.
- `sdl/sdlwindow.cpp` adds a Metal view to the window and hands bgfx its
  `CAMetalLayer`; the Windows message subclassing is left to Windows.
- `startup.cpp` has a `main()` that supplies `-DATADIR=` and `-USERDIR=` as
  described above and takes the single-instance lock with `flock`.
- `rawfile.cpp` turns `\` into `/` in file names.

## Limitations

- Saves and network games are compatible only with other 64-bit builds of the
  same platform, as with Win32 and x64 on Windows. Apple silicon and x86
  compute floating point identically under the flags used, but this has not
  been checked against an x64 Windows peer, so play LAN games against other
  Macs.
- The crash handler of the Windows build (`except.cpp`) is not ported. A crash
  ends the process with macOS's own crash report.
- Keyboard layouts are read through SDL. Keys whose names Windows takes from
  the active layout use SDL's names.
- The test harnesses under `tests/` are not built on macOS by default
  (`-DOPENTS_BUILD_TESTS=ON` enables them, untested).
