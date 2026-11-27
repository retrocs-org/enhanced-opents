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

The script builds and ad hoc signs a Release app under
`~/Development/builds/opents-mac`. The app is at `ninja/bin/OpenTS.app` with
Ninja or `make/bin/OpenTS.app` with Makefiles. Set `OPENTS_MACOS_BUILD_DIR`
to use another output directory. Add `--dmg` to write a disk image in that
same directory.

The signed app runs on the Mac that built it. Other Macs refuse an app with
only an ad hoc signature unless the user
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
6. writes the ZIP and the signed, notarized, and stapled DMG in the output
   directory as `OpenTS-macOS-<arch>.zip` and `OpenTS-macOS-<arch>.dmg`.

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

### Checking a distribution ZIP

On a separate Mac, download the ZIP through the same channel as players and
open the app in Finder. Gatekeeper should allow it to open. Check the downloaded
ZIP with the Xcode command line tools:

```sh
tools/macos/smoke-zip.sh /path/to/OpenTS-macOS-arm64.zip arm64 13.3 --distribution
```

Use `x86_64` for the Intel ZIP. The smoke test extracts the ZIP, checks the
bundle signature and notarization ticket, asks Gatekeeper to assess the app,
checks the advertised minimum and required UI files, and runs command-line
help. Run it on macOS 13.3 to verify that the executable starts on the
advertised minimum. Record the macOS version, architecture, and ZIP used.

## 4. Game data

`-DATADIR=` overrides the saved game-data folder. With neither an explicit
path nor a readable saved location, the app looks for `TIBSUN.MIX` in these
folders and uses the first that has it:

1. the folder that holds `OpenTS.app`;
2. `OpenTS.app/Contents/MacOS`;
3. `~/Library/Application Support/OpenTS/TiberianSun`.

With a selected data directory, the engine searches for bare file names in
the user directory, the data directory, and the configured search folders.

Copy the contents of a Tiberian Sun installation (the Ultimate Collection
release, or the 2010 freeware release) into one of them. Use the third for a
signed app in `/Applications`: putting files inside the bundle breaks its
signature. On macOS's default case-insensitive volume the file names need no
renaming.

Settings, saved games, screenshots and the single-instance lock go to
`~/Library/Application Support/OpenTS` unless `-USERDIR=` names another folder.
Logs go to the `Debug` folder there. Full-screen movies scale to fit the game
view while keeping their aspect ratio by default. Existing profiles can enable
this under **Options → Display → Stretch Movies**.

Loading screens and mission briefings scale to fit the window. On macOS,
**Game Controls** provides separate sliders for **Game Graphics** and
**Game UI Scale**. Game Graphics scales the battlefield and uses a render size
capped at 1280 × 800 at 100%. Game UI Scale sizes the build sidebar, radar,
and credits. Native uses the size implied by the configured resolution for
that slider. OpenTS applies the choices when you leave Game Controls and saves
them as `GraphicsScale` and `UIScale` under `[Video]` in `sun.ini`. An existing
profile starts both sliders at its previous combined setting. **Menu UI Size**
sets the size of in-game dialogs. Choose Small, Medium, or Large. Large is the
default, and the choice is saved as `MenuUISize`.

On high-refresh displays, camera and interface updates can be drawn between
simulation frames. OpenTS uses the active macOS display mode's refresh rate,
or the screen's reported maximum when macOS uses adaptive refresh. Solo
**Fastest**, **Faster**, and **Fast** target 75, 55, and 40 simulation frames
per second. Presentation can follow a 120 Hz display independently.

To pass options, start the executable inside the bundle:

```sh
/Applications/OpenTS.app/Contents/MacOS/OpenTS -DATADIR="$HOME/Games/Tiberian Sun"
```

Swipe with two fingers over the battlefield to pan in any direction. Scroll over
the sidebar to browse its build list. A conventional mouse wheel scrolls the
build list.

To play a mod prepared for OpenTS, keep its data folder and player directory
separate from the base game. Pass the mod’s game-data folder with
`-DATADIR=` and its settings and saves folder with `-USERDIR=`.
Original mods that depend on changes to the Windows executable may need
a compatibility port.

The first LAN game makes macOS ask whether OpenTS may use the local network.
Allow it; the choice can be changed later in System Settings > Privacy &
Security > Local Network.

## 5. Building in CI

`.github/workflows/macos.yml` builds both architectures with an ad hoc
signature on pushes and pull requests that change engine, build, test, or UI
files. It checks each extracted ZIP, runs the portable tests, and uploads
the verified ZIPs. To publish signed
builds from CI, import the Developer ID certificate (exported as a .p12) into
a temporary keychain in the job, create the notarytool profile from repository
secrets, and run the same `build.sh` command as above.

## How the port works

The engine is shared with the Windows build. Most POSIX compatibility code
lives in `code/platform/posix/`.

| File | Role |
| --- | --- |
| `wincompat.h`, `wincompat.cpp` | The Win32 types and functions the engine calls: files, directory scans, time, and text conversion. CMake forces the header into every engine source. |
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
- `sdl/sdlwincompat.cpp` implements Win32 key state and message boxes using SDL.
- `rawfile.cpp` turns `\` into `/` in file names.

When startup cannot find or load the archives, choose **Locate Game Data...**
and select the folder containing `TIBSUN.MIX`, or the folder containing both
`CACHE.MIX` and `CONQUER.MIX`. OpenTS remembers the folder after it finds
readable archives. If startup asks you to reopen the app, the new location
takes effect on the next launch. The error also lists the folders checked
and the debug log path.

### Import game archives from an ISO

In the app, choose **Locate Game Data...**, select one or more `.iso` files,
and click **Continue**. Choose a name and location for a new data folder, then
click **Import**. OpenTS remembers the imported folder and uses it on later
launches. Select the GDI, Nod, and Firestorm images together to import both
campaigns and the expansion. For shared SCORES, MULTI, and SIDECD archives,
the importer uses the base-game copies when present. Firestorm’s separate
archives, including SCORES01.MIX, are imported alongside them.

When selecting a game folder, choose **Use in Place** or **Import Copy…**.
Copies from ISO files and folders show the current file, progress, and elapsed
time.

From a checkout, you can also run the importer in Terminal:

```sh
tools/macos/import-iso.sh "$HOME/Library/Application Support/OpenTS/TiberianSun" \
    "/path/to/CD1_GDI.iso" "/path/to/CD2_Nod.iso"
```

Use **Locate Game Data...** if you chose a different destination. The importer
mounts each image read-only and copies MIX archives from the disc root and
`INSTALL` folder. Identical copies of an archive are kept once. If two images
have different versions of the same archive, import them into separate folders.
The importer needs a new destination and an existing parent directory.

### Use a mounted ISO in place

Mount the image in Finder. Use **Locate Game Data...** to select the folder
on the mounted volume that contains the game archives, or pass that folder
with `-DATADIR=`. OpenTS reads the files in place, so leave the image mounted
while you play. If the disc only has an installer or compressed installer
files, install or extract the game first and select the resulting folder.
Use the importer when the archives are split between the root and `INSTALL`.

The front-end menus keep their classic layout and scale to fit the window or
full-screen display. The selected game resolution still controls the size of
the tactical view.

## Playtest the packaged app

Extract the ZIP on the Mac you are testing. Use your own complete Tiberian Sun
installation and a fresh player directory so the test leaves existing settings
and saves alone.

```sh
player="$(mktemp -d)"
"/path/to/OpenTS.app/Contents/MacOS/OpenTS" \
    -DATADIR="/path/to/Tiberian Sun" -USERDIR="$player"
```

1. Check that the intro video plays with sound on the first launch.
2. Start a skirmish. Pan the map, select units, and give a move order to check
   mouse and keyboard input.
3. Resize the window, then switch to fullscreen and back. Check that the image
   fills the intended area and the pointer still selects what it points at.
   Switch to another app and return by clicking an OpenTS menu button. Check
   that the click opens that screen and that OpenTS stays in the background
   until you return.
4. Save during the skirmish, quit, then relaunch with the same player
   directory. Load the save and verify that the map and units resume as saved.

Run the sequence on Apple silicon and on a physical Intel Mac when available.
Keep the ZIP name, macOS version, architecture, and player directory with
the results. The player directory contains the logs and saves.

## Tests

Enable the portable C++ harnesses with `-DOPENTS_BUILD_TESTS=ON` and run
`ctest --test-dir ~/Development/builds/opents-mac/ninja -C Release --output-on-failure`
after building. The Windows harnesses require the Windows build. The macOS
workflow also runs the file-lookup tests with `TMPDIR` on a case-sensitive
APFS volume.

## Limitations

- The port compiles and starts on Linux with Clang against both libc++ and
  libstdc++, up to loading the game data. AppleClang builds both macOS
  architectures. Gameplay still needs separate verification.
- Saves and network games are compatible only with other 64-bit builds of the
  same platform, as with Win32 and x64 on Windows. Play LAN games against other
  Macs.
- The crash handler of the Windows build (`except.cpp`) is not ported. A crash
  ends the process with macOS's own crash report.
- Keys whose names Windows takes from the active keyboard layout use SDL's
  names.
