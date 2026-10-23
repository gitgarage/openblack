# openblack for Nintendo Switch

A homebrew port of [openblack](https://github.com/openblack/openblack), the open source reimplementation of
Lionhead's **Black & White (2001)**, to the Nintendo Switch. (openblack's own README is
[README-openblack.md](README-openblack.md).)

It runs the original game's first island, Land 1, from start to finish on a Switch running custom firmware,
using the data files from your own copy of the PC game. It plays at about 44 frames a second.

> **You need your own copy of Black & White.** This port contains no game data. It reads the `Data`, `Audio` and
> `Scripts` folders of an original PC installation, which you copy to your SD card yourself.
>
> This is an unofficial fan project. It is not affiliated with or endorsed by Lionhead Studios, Electronic Arts or
> Nintendo. "Black & White" is a trademark of its owner.

---

## What you need

- A Nintendo Switch that can run homebrew, with [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) custom
  firmware and the Homebrew Menu (hbmenu).
- An SD card with about **1 GB** free (the game data is about 700 MB).
- An installed copy of the original **Black & White** for PC (the 2001 retail release). You only need three
  folders from its installation directory.

## Installing

1. **Copy the program.** Download `openblack.nro` from the
   [Releases page](https://github.com/gitgarage/openblack/releases/latest) and put it here on the SD card:

   ```
   sdmc:/switch/openblack/openblack.nro
   ```

2. **Copy your game data.** Find your Black & White installation folder on your PC (usually
   `C:\Program Files\Lionhead Studios Ltd\Black & White`). Copy its `Data`, `Audio` and `Scripts` folders into
   `sdmc:/switch/openblack/game/`, so that the card looks like this:

   ```
   sdmc:/switch/openblack/
   ├── openblack.nro
   └── game/
       ├── Audio/
       ├── Data/
       └── Scripts/
   ```

   Copy the folders whole. Nothing in them needs converting, and the intro movie (`Data/INTRO.bik`) is played
   straight from the original file.

3. **Launch it with full memory.** openblack needs the memory a game gets, which the Album does not have
   (launched from the Album, it runs out of memory while loading). Instead:
   - hold **R** while you start any installed game from the HOME menu, which opens the Homebrew Menu with full
     memory (Atmosphère's title override);
   - choose **openblack**.

Loading takes about half a minute. The game starts straight into Land 1's opening.

### Updating

Download the newest `openblack.nro` from the [Releases page](https://github.com/gitgarage/openblack/releases) and
replace `sdmc:/switch/openblack/openblack.nro` with it. Your game data and save points stay where they
are.

### Files the game writes

| File | What it is |
|---|---|
| `sdmc:/switch/openblack/openblack.log` | the log of the last session (useful in bug reports) |
| `sdmc:/switch/openblack/savepoints/` | your two save points (see the D-pad below) |

---

## Controls

### Buttons

| Button | What it does |
|---|---|
| **Left stick** | Move the camera across the land |
| **Right stick** | Turn the view left and right, tilt it up and down |
| **R / L** | Zoom in / zoom out (held) |
| **ZL** | The **Action Button**: pick up, collect, give, let go, throw (see below). With the hand on or near the Temple's door: go in |
| **ZR** | While holding something: the Action Button too, as ZL |
| **Long press on an area of the ground, then drag** | Drags the user in that direction |
| **A** | Your creature's leash: none, regular, reward (good), punishment (evil), one step each press |
| **X** | Bring your creature to where your hand is |
| **Y** | Switch between the time of day's own light and broad daylight (for seeing at night) |
| **B** or **−** | Leave the Temple, or leave the side-on fight view |
| **+** | Pause / unpause |
| **D-pad up / left** | Save to save point 1 / 2 |
| **D-pad down / right** | Load save point 1 / 2 |
| **Right stick press** | Play faster (twice as fast each press, up to ×8) |
| **Left stick press** | Play slower (half as fast each press, down to ×1/8) |
| **Any button** | Skip the intro movie |

Normal speed (shown as ×1) is the original game's own pace: ten game turns a second, so scripted scenes, walks
and camera moves take the time they take in the original.

### Touchscreen

Your hand follows your finger. What a touch does depends on how it starts:

| Touch | What it does |
|---|---|
| **Quick tap** | Click: on a scroll, a person, your creature, a miracle, or a place |
| **Touch and move straight away** | Move the hand without grabbing (for pointing and drawing) |
| **Long press** (hold still briefly) | Grab the land and drag it. On wood or grain: collect it while you hold. On a scroll: start its challenge. On the Temple door: go in. On a miracle: pick it up. With the leash on, on a tree or house: tie your creature there (again to untie). **While holding something:** let go of it there, straight into the village store if you press on one |

### The hand

- **Pick something up:** press ZL on or near it. If you are not quite on it, the nearest thing you can pick up
  close to where you pressed is taken. People, animals, rocks, logs, trees and fences can all be picked up.
- **Throw:** while holding something, swipe and let go of the button. The faster the swipe, the further it flies.
  Let go with your hand still to put it down gently. Things fly as in the original: rocks fly far, while trees,
  people and miracles slow in the air and land shorter.
- **Collect wood or grain:** hold ZL (or long-press) over a wood pile, a grain field, or the wood or grain half of
  a village store. The amount grows the longer you hold, up to what the hand can carry.
- **Give wood or grain:** press ZL holding it over the village store to put it straight into the store, or let
  go of wood over a building site to build with it.
- **Trees:** pull one up with ZL. Put it down gently to plant it somewhere else, throw it to knock it down on its
  side, or drop or throw it onto the village store or a building site to shred it into wood. Fences shred the
  same way.
- **Miracles:** take a miracle sphere (tap it, or ZL on it), then tap where it should happen, or carry it there
  and let go. Food and wood miracles fill the nearest store; heal works where it lands.
- **Your creature:** press ZL on your creature with an empty hand and move the hand to stroke it (reward) or slap
  it (punish). Hand it food or a person to feed it. With a leash on, ZL sends it to walk, fetch, or be tied.
- **Fights:** tap your opponent to attack, tap your own creature to block. B leaves the side view.

---

## What works

Land 1 can be played from the opening to the portal at its end. Over the course of the port:

**Running on the Switch**
- openblack builds for the Switch unchanged, through its own CMake, with devkitPro's toolchain.
- Rendering through bgfx's OpenGL ES 3.1 backend on Mesa, with skinned (animated) models, at about 44 FPS with
  minimal graphics (no bump maps). The sea reflects only the sky, as the original's does with its land
  reflection turned off.
- Joy-Con and touchscreen controls mapped onto openblack's own input.
- Full-memory launch and a log on the SD card.
- The game clock follows the Switch's clock, as the original followed the PC's.

**The story and its scripts**
- The original campaign scripts (`challenge.chl`) run in openblack's script engine. Hundreds of the original's
  script functions were implemented for Land 1: the scripted camera and cutscenes, dialogue, the opening family
  and its shark scene, scrolls and their challenges, the Temple and its rooms, singing stones, gate totems,
  the leash and creature lessons, the lost flock, the sculptor, the missionaries, the drowning men, Pied Piper and
  more.
- The intro movie (`INTRO.bik`) plays at the point the original plays it, with its sound, and can be skipped.
- Music and sound effects from the original's sound banks, placed in the world.
- Spoken dialogue: each scripted line plays its recorded voice (Guidance, your advisors, the villagers), matched
  to its line by the text table recovered from the original `runblack.exe`.
- The original's camera positions, camera paths and hand demonstrations (the tutorial's recorded hand) are read
  from the game's own data files.

**The opening**

The opening, from the fade-in on the beach to taking control on the path, was compared shot by shot with the
original running in Wine and rebuilt from the original's code where it differed:
- The screen fades in from black at the start and out to black before the intro movie, as the scripts ask.
- The family walks, the sharks swim and the camera moves at the original's speeds. The sharks follow their
  recorded paths, head first, at twice their model's size, leaving white wakes in the water.
- After the movie the god's hand arrives as the original's fire ball. It streaks down out of the sky and lands in
  the sea, then a hand rises from the water, picks up the drowning boy, and sets him down beside his parents.
- The sun hangs low over the sea at dusk. Its glare shows through gaps in the palms, as in the original.
- Your two advisors, the Yogi and the Demon, are built from the original's own files. They fly in, hover with
  flapping wings, come closer when they speak, point at what they talk about, and fly home when a conversation
  ends.
- Subtitles show each spoken line along the bottom in the original's own fonts. The Demon's lines are pink
  hand-lettering, the Yogi's a cream serif and the villagers' white.
- The camera follows the original's views: kept above the land, following and facing people, and zooming.
  The coast is drawn at sea level, as the original draws it, so the scripted camera shots line up with the
  original's.
- Dawn turns to day over the original's time, in the original's colours.

**Your creature**
- Choose the Ape, Cow or Tiger in the glade. Your creature walks, eats, sleeps, poos and wanders on its own.
- Stroke and slap it, lead it on the leash, tie it up, feed it, and teach it to fight.
- It moves as the original's does: each animation plays over its own length, its bones are posed with the
  original's maths, it eases between standing and walking as it speeds up and slows down, and its feet keep to the
  ground as it walks.

**Your hand**
- The hand's poses are the original's, posed the original's way: its fingers wiggle when idle and get ready over
  something it can take. It holds things beside it, between thumb and forefinger, closing round them by their
  size. Its fingers trail behind it as it moves.

**The world**
- Villagers live their days as in the original: they work, rest and chill out, get hungry and eat from the
  village store, housewives fetch food home for dinner, and they go home to sleep at night and come out in the
  morning. They walk at the original's pace, and their walking and running keep pace with the ground, so their
  feet never slide, including when a scene moves them.
- Farmers sow and harvest the grain fields; villagers build: they fetch wood from the store, carry it to the site
  and build the Temple and other buildings.
- People dance around the statue at the start, to the original's own choreography.
- Reward chests: the original's treasure chest falls from the sky where the scripts give one; click it and its lid
  swings open with the original's sounds, giving food, wood or a miracle.
- Towns believe in you from what they see (the original's belief numbers), and can be won with miracles.
- Animals stand in their flocks (a lost one joins a flock of its kind), and can be picked up and thrown.
- Miracles: food, wood, heal, shield and more, from spheres and dispensers (each sphere resting on its dispenser). A sphere looks as in the original:
  its animated glassy bubble with the miracle's icon turning upright inside, and, for water and fire, the
  original's own particle effects (the water's mist, the fire's glow and flame), run from the game's effect files.
- Grain fields hold grain you can collect, empty to nothing, and grow back over a game day.
- Trees can be uprooted, replanted, thrown, and shredded into wood.
- Thrown people and animals can be hurt or killed; the dead lie where they fell and later become skeletons.
- Far-off people, animals and trees are drawn as their shadows to keep the frame rate up.

## Known limitations

- **Only Land 1.** Moving to the next island (the original's `LOAD_MAP`) is not done yet, so the game stops at the
  portal.
- **The missionaries' ship** does not sail away at the end of their quest; in the original the engine itself does
  this.
- **Gestures** are not used for casting miracles yet. Miracles are cast from spheres.
- **Particle effects:** the original's effect system is being ported a piece at a time. The water and fire
  spheres use it; the heal, lightning, shield and teleport spheres show their icon without their effect so far.
- Some objects do not topple when hit, and thrown things pass through trees.
- **Camera shake** in scripted scenes is not done yet.
- **The intro's clouds and boat** and the camera bookmarks the opening script asks for are not drawn yet.
- Saving uses two test save points on the D-pad, not the original's save menu.

Bug reports are welcome. Please include `sdmc:/switch/openblack/openblack.log`, and say what you were doing.

---

## Building from source

The Switch build runs inside Docker, using devkitPro's `devkitpro/devkita64` image (which includes SDL2, Mesa,
OpenAL Soft, Bullet, zlib, minizip and FFmpeg for the Switch).

1. Clone this repository, and the pinned dependencies listed in `switch-port/deps.txt` into `switch-port/deps/`
   (spdlog, EnTT, Dear ImGui, cxxopts, stb, dr_libs, glm, fmt). bgfx is in `switch-spike/deps/bgfx.cmake`.
2. Build bgfx's shader compiler for your PC once: `switch-port/build-host-tools.sh`
3. Build the game: `RELEASE=1 switch-port/build.sh`

The result is `switch-port/build-release/src/openblack.nro`. `build.sh` applies the bgfx patches in
`switch-port/patches/` the first time.

The developers' own builds add a debugging link over the USB-C cable (a live log, crash reports, and new builds
sent from a PC). It needs a separate `atlas-usb-link` checkout next to this repository and is not part of
releases: `build.sh` only builds it in when that checkout exists and `RELEASE=1` is not set (the CMake option
`OPENBLACK_SWITCH_DEV_LINK`).

How the port fits onto openblack: `switch-port/switch-setup.cmake` is injected into openblack's own CMake
(`CMAKE_PROJECT_INCLUDE`), supplies the Switch libraries, and builds the NRO. `switch_main.cpp` starts the log and
passes the game path, `switch_input.cpp` maps the Joy-Con and touchscreen, and `switch_update.cpp` (developer
builds only) receives new builds over the USB cable.

---

## License

This port is licensed under the **GNU General Public License, version 3**, the same license as openblack. See
the [`license`](license) file at the root of this repository for the full text.

Under the GPL, anyone who distributes `openblack.nro` must also make its complete source code available under the
same license.

The libraries it is built with keep their own licenses: bgfx, bx and bimg (BSD 2-Clause), SDL2 (zlib), OpenAL Soft
(LGPL 2.1+), FFmpeg (LGPL 2.1+, or GPL depending on how devkitPro built it), dav1d (BSD 2-Clause), Bullet,
zlib and bzip2 (zlib-style), Dear ImGui, EnTT, spdlog, fmt, glm and cxxopts (MIT), stb and dr_libs (public domain /
MIT), and libnx (ISC).

## Credits

- The [openblack developers](contributors.md), whose engine this is.
- The [bw1-decomp](https://github.com/openblack/bw1-decomp) project, whose decompilation of the original game
  made the villagers' lives, the dances, creature and hand animation, throwing and the miracle spheres match the
  original.
- Lionhead Studios, for Black & White.
- The devkitPro, libnx and Atmosphère projects, which make Switch homebrew possible.
