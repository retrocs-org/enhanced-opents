# Building OpenTS for macOS

The macOS port builds `OpenTS.app`, a native arm64 or x86_64 application that
renders through Metal. It needs the original Tiberian Sun data files, the same
ones the Windows build reads.

## Requirements

| Component | Requirement |
| --- | --- |
| macOS | 13.3 (Ventura) or newer, to build and to run |
| Processor | Apple silicon (arm64) or Intel (x86_64 with SSE4.2) |
| Compiler | Xcode command line tools (`xcode-select --install`), AppleClang 15 or newer |
| CMake | 3.23 or newer (`brew install cmake`) |
| Ninja | Optional, faster (`brew install ninja`) |
| Python 3 | The one the command line tools install, used to compile the string table |
| Signing | For distribution: an Apple Developer Program membership and a Developer ID Application certificate |

The build produces an app for the processor of the Mac it runs on. Build on an
Apple silicon Mac for arm64 and on an Intel Mac (or a `macos-15-intel` CI
runner) for x86_64.

## 1. Unpack the source

The source archive holds the whole repository, its git history and every
submodule under `thirdparty/`, so the build needs no network access:

```sh
tar -xzf OpenTS-macos.tar.gz
cd OpenTS
```

From a git checkout instead: `git submodule update --init --recursive`.

## 2. Build for your own Mac

```sh
tools/macos/build.sh
```

This configures `build-macos/`, builds the Release app, and gives it an ad hoc
signature. The result is `build-macos/bin/OpenTS.app`. It runs on the Mac that
built it. Other Macs refuse an app with only an ad hoc signature unless the user
right-clicks it and chooses Open, or clears the quarantine flag with
`xattr -dr com.apple.quarantine OpenTS.app`.

`tools/macos/build.sh Debug` builds with debug information and no optimization.

## 3. Build a signed and notarized app

To give the app to other people without Gatekeeper warnings, sign it with a
Developer ID certificate and have Apple notarize it.

### One-time setup

1. **Create the certificate.** In Xcode, open Settings > Accounts, select your
   team, choose Manage Certificates, and add a **Developer ID Application**
   certificate. Only the team's Account Holder can create one. You can also
   create it at developer.apple.com > Certificates, from a signing request
   made in Keychain Access.

2. **Find its exact name.**

   ```sh
   security find-identity -v -p codesigning
   ```

   The line to use reads like
   `"Developer ID Application: Tommy Example (ABCDE12345)"`. The ten
   characters in parentheses are your Team ID.

3. **Store notary credentials in the keychain.** Make an app-specific
   password at account.apple.com > Sign-In and Security > App-Specific
   Passwords, then:

   ```sh
   xcrun notarytool store-credentials OpenTS-notary \
       --apple-id "you@example.com" \
       --team-id ABCDE12345 \
       --password "abcd-efgh-ijkl-mnop"
   ```

   `OpenTS-notary` is the profile name the build uses from now on. An App
   Store Connect API key works too (`--key`, `--key-id`, `--issuer`).

### Every release

```sh
tools/macos/build.sh Release \
    --sign "Developer ID Application: Tommy Example (ABCDE12345)" \
    --notarize OpenTS-notary \
    --zip --dmg
```

The script:

1. builds the app;
2. signs it with the hardened runtime, a secure timestamp and the
   entitlements in `tools/macos/OpenTS.entitlements` (none are needed; the
   file is there so none are picked up by accident);
3. checks the signature with `codesign --verify --strict`;
4. sends the app to Apple's notary service and waits for the verdict,
   usually a few minutes;
5. staples the ticket to the app, so Gatekeeper accepts it offline, and
   confirms with `spctl --assess`;
6. writes `build-macos/OpenTS-macOS-<arch>.zip` and a signed, notarized and
   stapled `build-macos/OpenTS-macOS-<arch>.dmg`.

The identity and profile can come from the environment instead:

```sh
export OPENTS_SIGN_IDENTITY="Developer ID Application: Tommy Example (ABCDE12345)"
export OPENTS_NOTARY_PROFILE=OpenTS-notary
tools/macos/build.sh --zip --dmg
```

`--sign` without `--notarize` gives a signed app that still makes Gatekeeper
warn on other Macs; that is useful for checking the signature before the first
notarization.

### When notarization fails

The script stops when Apple rejects the submission. Read the reason with the
submission ID that `notarytool` printed:

```sh
xcrun notarytool log <submission-id> --keychain-profile OpenTS-notary
```

The usual causes are a certificate other than Developer ID Application, a
missing `--timestamp` or hardened runtime (the script always passes both), or a
file changed inside the bundle after signing. Rebuild rather than editing the
bundle by hand.

### Checking a finished app

```sh
codesign -dv --verbose=4 build-macos/bin/OpenTS.app   # who signed it, with which flags
spctl --assess --type execute --verbose=2 build-macos/bin/OpenTS.app
xcrun stapler validate build-macos/bin/OpenTS.app
```

`spctl` should report `accepted` and `source=Notarized Developer ID`.

## 4. Game data

Without `-DATADIR=`, the app looks for `TIBSUN.MIX` in these folders and uses
the first that has it:

1. the folder that holds `OpenTS.app`;
2. `OpenTS.app/Contents/MacOS`;
3. `~/Library/Application Support/OpenTS/TiberianSun`.

Copy the contents of a Tiberian Sun installation (the Ultimate Collection
release, or the 2010 freeware release) into one of them. Use the third for a
signed app in `/Applications`: putting files inside the bundle breaks its
signature. On macOS's default case-insensitive volume the file names need no
renaming.

Settings, saved games, screenshots and the single-instance lock go to
`~/Library/Application Support/OpenTS` unless `-USERDIR=` names another folder.
Logs go to the `Debug` folder there.

To pass options, start the executable inside the bundle:

```sh
/Applications/OpenTS.app/Contents/MacOS/OpenTS -DATADIR="$HOME/Games/Tiberian Sun"
```

The first LAN game makes macOS ask whether OpenTS may use the local network.
Allow it; the choice can be changed later in System Settings > Privacy &
Security > Local Network.

## 5. Building in CI

`.github/workflows/macos.yml` builds both architectures with an ad hoc
signature on every push that touches the engine, and uploads each zip. To
publish signed builds from CI, import the Developer ID certificate (exported
as a .p12) into a temporary keychain in the job, create the notarytool profile
from repository secrets, and run the same `build.sh` command as above.

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
  bits. On Windows the two spellings have the same width.
- `sdl/sdlwindow.cpp` adds a Metal view to the window and hands bgfx its
  `CAMetalLayer`; the Windows message subclassing is left to Windows.
- `startup.cpp` has a `main()` that supplies `-DATADIR=` and `-USERDIR=` as
  described above, takes the single-instance lock with `flock`, and reads the
  UI files from the bundle's Resources folder.
- `rawfile.cpp` turns `\` into `/` in file names.

## Limitations

- The port compiles and starts on Linux with Clang against both libc++ and
  libstdc++, up to loading the game data. It has not yet been compiled with
  AppleClang or played on a Mac; the macOS-only code (the Metal view, the
  bundle paths, the system version lookup) is where a first build may need
  fixes.
- Saves and network games are compatible only with other 64-bit builds of the
  same platform, as with Win32 and x64 on Windows. Play LAN games against other
  Macs.
- The crash handler of the Windows build (`except.cpp`) is not ported. A crash
  ends the process with macOS's own crash report.
- Keys whose names Windows takes from the active keyboard layout use SDL's
  names.
- The test harnesses under `tests/` are not built on macOS by default
  (`-DOPENTS_BUILD_TESTS=ON` enables them, untested).
