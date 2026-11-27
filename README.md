<p align="center">
  <img src="https://raw.githubusercontent.com/OpenTS-Developers/.github/main/assets/opents-logo.png" alt="OpenTS" width="512">
</p>

<p align="center">
  <a href="https://github.com/OpenTS-Developers/OpenTS/releases"><img src="https://img.shields.io/github/downloads/OpenTS-Developers/OpenTS/total?label=downloads" alt="Downloads"></a>
  <a href="https://github.com/OpenTS-Developers/OpenTS/actions/workflows/engine.yml"><img src="https://github.com/OpenTS-Developers/OpenTS/actions/workflows/engine.yml/badge.svg" alt="Engine build"></a>
  <a href="https://opents-developers.github.io/OpenTS/"><img src="https://github.com/OpenTS-Developers/OpenTS/actions/workflows/manual-pages.yml/badge.svg" alt="Manual"></a>
  <a href="https://www.patreon.com/c/ZivDero"><img src="https://img.shields.io/badge/Patreon-ZivDero-F96854?logo=patreon&logoColor=white" alt="Patreon"></a>
</p>

# OpenTS Enhanced

OpenTS is a community-led, open-source reconstruction of *Command & Conquer:
Tiberian Sun*. Instead of patching or extending the retail executable, it
rebuilds the engine as a standalone program.

OpenTS gives equal weight to two goals: maintaining a playable engine and
providing a capable platform for modding and engine development. Work on one
goal should not come at the expense of the other.

OpenTS is:

- an independent, community-led source reconstruction targeting Tiberian Sun
  2.03 Firestorm;
- a playable engine based on Electronic Arts' GPL-released source for related
  Command & Conquer games and Tiberian Sun-specific reverse engineering; and
- the active base for maintenance, documentation, modernization, bug fixes,
  and new modding capabilities.

OpenTS is not:

- a remaster or remake;
- an official Electronic Arts source release; or
- a distribution of the original game assets.

OpenTS is an independent community project and is not affiliated with or
endorsed by Electronic Arts.

## Community

- Discord: <https://opents.net/discord>
- Bug reports and proposals:
  [GitHub issues](https://github.com/OpenTS-Developers/OpenTS/issues)

## Downloads

- **Releases** are the recommended builds. Every release on the
  [releases page](https://github.com/OpenTS-Developers/OpenTS/releases) carries
  a zip per platform, `OpenTS-<version>-Win32.zip` and
  `OpenTS-<version>-x64.zip`, each containing `Game.exe`, `Language.dll`, and
  `Game.pdb`. The 32-bit build runs on both 32-bit and 64-bit Windows and has
  the longer runtime history; the 64-bit build runs on 64-bit Windows only.
- **Nightly builds** are development snapshots from the
  [Engine nightly](https://github.com/OpenTS-Developers/OpenTS/actions/workflows/engine-nightly.yml)
  workflow. Download the latest one without a GitHub account through
  [nightly.link](https://nightly.link/OpenTS-Developers/OpenTS/workflows/engine-nightly/main).
  Nightlies contain the latest merged changes without release validation and
  expire after 90 days.

## Installing

1. Install Tiberian Sun from Command & Conquer The Ultimate Collection on
   Steam or the EA App.
2. Extract the release zip into the Tiberian Sun game directory.
3. Run `Game.exe`.

OpenTS supports Windows 10 version 1903 (build 18362) and newer. Earlier
Windows versions are untested and unsupported. Wine may work, but there is no
supported native Linux build. The processor must support SSE4.2; see
[Supported target](docs/BUILDING.md#supported-target).

Keep a saved game with the platform that wrote it, and play a network game with
peers on the same platform. The 32-bit and 64-bit builds write saves and
network packets at their own pointer widths, and neither checks which platform
produced what it is reading, so a mismatch surfaces as a failed load or a
desync.

OpenTS supplies the engine, not the game data: the installation above
provides the original assets. There is no installer, and no extra runtime
library or launch argument is required.

## Documentation

The [OpenTS manual](https://opents-developers.github.io/OpenTS/) documents
setup, runtime behavior, INI configuration, mapping, and source-level
internals.

### Map Editor

From the main menu, choose **Options → Map Editor**. **New Map** opens a setup form for
the map name, width, height, and temperate or snow theater. Set each dimension
from 32 to 128. **Sample** opens Twin Groves, a 64×64 temperate map with
two starting positions and Tiberium trees.

To edit an existing `.map` or `.mpr`, choose **Open** in the command bar,
enter its path, then select **Open Map**.
The editor renders the map with the installed game's artwork. Scroll over
the canvas with a mouse wheel to zoom around the pointer. On macOS, pinch
to zoom and scroll with two fingers to pan the zoomed map. You can also pan
by dragging with the right or middle mouse button. Click the minimap to center the view there, or choose **Fit**
to show the whole map again. **Grid** shows the
cell boundaries. The editor keeps a cell view available while the artwork
is loading.
In **Points**, choose **P1–P8** and click a cell to place or move a player
start. Each start has its own color and number. Use the waypoint ID field for
other points. You can also enter its coordinates and choose **Set**, or use
**Clear** to unset it. **Save** writes the working map. **Save As** starts
working in a new `.map` or `.mpr` file, and **Export Copy** writes a separate
file. Save As and Export Copy require an unused filename. Use Ctrl+S or
Command+S to save, or add Shift to open Save As. The editor asks before you
discard unsaved changes.

The editor saves a recovery draft shortly after an edit. If the editor closes
unexpectedly, use **Recover Draft** from its welcome screen to reopen the
latest snapshot. Use Save As to keep the restored map. Saving the working map
or discarding the active draft clears its recovery snapshot.

In **Tiles** and **Objects**, search by name or filename, or select a category.
The palette previews artwork from the map's theater. Select an asset and
hover over the map to preview its placement. Click to stamp a complete terrain
template, such as a road, cliff, bridge, or shoreline piece. Stamps require
room for every cell. For single-cell tiles, choose a brush size from 1 × 1
through 5 × 5 and drag to paint. Undo treats each stamp or drag as one edit.
Tile painting supports maps with an IsoMapPack5 or no terrain pack. **Objects**
places scenery one at a time on maps using the current scenario format. Choose
**Select** in the Objects tab and click a scenery object's cell to select it.
Choose **Move** and click an empty cell, or choose **Remove**. Undo and Redo
also apply to scenery edits. Exports keep other scenario sections. **Undo** reverses a whole stroke, waypoint change,
tree placement, or applied script edit. **Redo** reapplies it. Saving keeps the undo history available.

**Units** places vehicles, structures, infantry, and aircraft using the
scenario rules. Choose a subcell from 0 through 4 for infantry. In **Select**
mode, click an object to edit its properties. Click any cell within a
structure’s footprint to select it. Drag to move it, or hold Shift while
dragging to duplicate it. Red cells in the drag preview mark obstructions.

**Cell Tags** paints or erases existing trigger tags on the map. Turn on
**Markers** to see tagged cells, or use **Inspect** to open the trigger
referenced by a tagged cell. **Balance** estimates power supply and demand
by house and Tiberium value within 12 cells of each player start.

**Lighting** edits the normal and ion-storm settings and renders
the selected lighting mode after you apply it.

**Auto Borders** updates crystal and swamp transition tiles as part of the
current paint stroke when the theater supplies the transition sets.
In **Tunnels**, click cells to trace a route or click either endpoint of an
existing tunnel to edit it. Use **Trim**, **Reverse**, and **Apply** to change
the route. Place matching entrance and exit terrain tiles separately.

**Overlays** paints green or blue Tiberium and concrete walls, or erases
overlay cells. Set the Tiberium growth stage and brush size before painting.
**Regions** copies terrain, overlays, and objects as independent layers. Select two corners,
click a paste destination to preview the footprint, then choose **Paste Here**.
An elevation offset applies to the copied terrain.

Use **Water** with **Shores** to place fitting shoreline templates around
freshly painted water. **Houses** edits credits, alliances, colors, tech level,
and IQ for existing scenario houses. **Bounds** previews changes to the
playable area and counts starts, objects, tags, and endpoints outside it.
Map dimension changes keep existing cell coordinates and are blocked when
saved cells would fall outside the new map.

**Scripts** edits the native scenario records for triggers, events, actions,
tags, cell tags, teams, task forces, team scripts, tutorial text, lighting,
terrain objects, structures, and units. Choose a waypoint and delay and select
**Add Lightning** to create a timed skirmish trigger. Apply each manual
section before switching sections, then export the map. See the
[scenario trigger format](https://opents-developers.github.io/OpenTS/formats/scenario-triggers/)
for the record layout.


**Visual Builder** in Scripts creates triggers with timed or object events and
lightning, team, reinforcement, or mission outcome actions. It can create and
edit team definitions linked to existing task forces and scripts. Select an
existing waypoint on the map or enter its ID. Raw Records remains available.

**Validate** reports broken trigger, tag, and team links, potential spawn
obstructions, overlapping buildings, and tunnel route errors. Select an issue
to inspect its location or record. **Test Play** launches the current draft
as a skirmish with a chosen start and opponent count, or as a scripted mission
using the map’s player house. Leaving the match returns to the draft.


## State and plans

Release 0.1.0 runs the full Tiberian Sun 2.03 Firestorm game. The GDI, Nod,
and Firestorm campaigns, skirmish, and save/load have received full
play-through testing. LAN multiplayer has had more limited testing. No
user-visible regression from the original game is currently known. The
renderer uses
[bgfx](https://github.com/bkaradzic/bgfx) and supports modern resolutions
through 4K, including ultrawide.

The first of the project's three development milestones is the current focus:

1. CnCNet and CnCNet client support, including porting the parts of
   [ts-patches](https://github.com/CnCNet/ts-patches) this requires.
2. Feature parity with
   [Vinifera](https://github.com/Vinifera-Developers/Vinifera) and the rest
   of ts-patches.
3. Extending Tiberian Sun with new features, striving toward feature parity
   with Red Alert 2 and Yuri's Revenge, and growing engine capabilities that
   match or exceed the popular Yuri's Revenge engine extensions.

Alongside these goals, the engine is modernized incrementally toward an
entity-component architecture, and new development is shaped so that
migration stays possible. [Project direction](docs/DIRECTION.md) explains the
reasoning.

## Building

OpenTS builds for 32-bit and 64-bit Windows with Visual Studio 2022 and CMake.
[Building OpenTS](docs/BUILDING.md) documents the exact requirements,
commands, and outputs.

A native macOS build (Apple silicon and Intel, rendering through Metal) is
made with the Xcode command line tools and CMake; see
[Building OpenTS for macOS](docs/BUILDING-macOS.md).

## Contributing

Bug reports, proposals, documentation improvements, and focused pull requests
are welcome. Review capacity is limited, so discuss non-trivial work with the
maintainers before implementing it. [CONTRIBUTING.md](CONTRIBUTING.md)
explains the current priorities and review policy; source conventions are in
[Style](docs/STYLE.md).

## Origins

OpenTS continues the community reconstruction preserved in the
[TibSun archive](https://github.com/OpenTS-Developers/TibSun), built from
Electronic Arts' published source for related Command & Conquer games and
completed through reverse engineering against the original executable.
[History](docs/HISTORY.md) records the reconstruction's lineage and methods.

## License and acknowledgements

OpenTS is licensed under the GNU General Public License, version 3 or later.
Material derived from Electronic Arts source remains subject to the additional
GPL Section 7 terms in [LICENSE.md](LICENSE.md).
[Third-party notices](THIRD_PARTY_NOTICES.md) identify bundled dependencies
and their licenses.

[ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md) thanks the people, projects, and
communities whose work made OpenTS possible.
