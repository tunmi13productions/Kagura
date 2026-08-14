# Kagura

Screen reader accessibility for **NARUTO SHIPPUDEN: Ultimate Ninja STORM 4** on PC.

Kagura speaks the game's menus, character select, command lists, battle missions and story dialogue through your screen
reader, so the game can be navigated and played without sight.

It is a plugin for the [Storm Framework / NS4 Modding API](https://github.com/Sim-Sim2000/ns4moddingapi), and speaks
through [Prism](https://github.com/ethindp/prism), which supports NVDA, JAWS, SAPI, Windows OneCore, UIA, ZoomText,
System Access, ZDSR, PC-Talker, Sense Reader, BoyPCReader and Window-Eyes.

---

## Installing (no building required)

1. **Install the Storm Framework** into your STORM 4 folder, if you have not already. That is the folder containing
`NSUNS4.exe`, typically `...\steamapps\common\NARUTO SHIPPUDEN Ultimate Ninja STORM 4`. You should end up with a
`moddingapi` folder and an `xinput9_1_0.dll` beside the game executable.

2. **Download the latest `kagura_*.zip`** from the [Releases page](../../releases).

3. **Copy the `Kagura` folder from the zip** into `moddingapi\mods\`. Relative to the folder holding `NSUNS4.exe`, you
should end up with these four files:

```
moddingapi\mods\Kagura\Kagura.dll
moddingapi\mods\Kagura\info.txt
moddingapi\mods\Kagura\names.txt
moddingapi\mods\Kagura\lib\prism.dll
```

4. **Start the game.** A few seconds in you should hear **"Kagura ready"**.

If you do not hear it, open `moddingapi\mods\Kagura\kagura.log` — it records which screen reader was detected and
whether anything failed. That file is rewritten at every launch.

### Uninstalling

Delete `moddingapi\mods\Kagura`. Kagura does not modify any game files.

---

## Using it

Move through menus as normal and Kagura announces the highlighted item.

- `F8` — mute / unmute announcements
- `F9` — repeat the last announcement

`F9` works while muted, so you can silence Kagura during a fight and still check what you missed. Neither key is taken
away from the game — Kagura only observes them.

### Tuning what gets spoken

`names.txt` in the mod folder controls the wording, and is plain text you can edit yourself. Format:

```
gamemodeselect_018 = Story        # give a message ID a spoken label
presence_* = -                    # never speak these (trailing * matches a prefix)
c_cha_* = ^                       # speak first when several messages arrive at once
battle_preset_* = +               # speak as-is, overriding a broader wildcard rule
@icon:btn_attack = Attack         # translate a button icon into a word
```

Anything with no rule is still spoken using the game's own text, so an unmapped menu is never silent.

---

## Why a names file exists

The game resolves exactly one message per menu selection change, and that message is the highlighted item's
*description* — never its name. The names themselves are pre-rendered `.dds` textures inside the Scaleform UI, so they
never pass through the text system at all. `names.txt` supplies the missing labels.

That is why an unmapped menu still reads as, say, *"Enjoy free battles."* rather than *"Free Battle"* — the description
is genuinely all the game exposes.

---

## Building from source

**Requirements**

- Windows 10 or later, x64
- Visual Studio 2022 Build Tools, or any 2022 edition
- The **Desktop development with C++** workload, or at minimum the MSVC x64 toolset
- The **C++ ATL** component (`Microsoft.VisualStudio.Component.VC.ATL`), required by Prism's JAWS, SAPI, ZoomText, Window-Eyes and Sense Reader backends. Without it those five fail to compile.
- CMake 3.25+ and Git on `PATH`

To add ATL to an existing install:

```
"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vs_installer.exe" modify --installPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" --add Microsoft.VisualStudio.Component.VC.ATL --passive --norestart
```

**Build**

```powershell
# One time: build Prism and place prism.dll in lib\
.\scripts\build-prism.ps1

# Build Kagura and install it straight into the game
.\build.ps1 -GamePath "D:\SteamLibrary\steamapps\common\NARUTO SHIPPUDEN Ultimate Ninja STORM 4"

# Or build without installing
.\build.ps1 -NoInstall

# Or produce a release zip in dist\
.\build.ps1 -Package -Version v1.0.0
```

`build.ps1` finds your toolchain through `vswhere`, so no paths are hardcoded. The game must be **closed** to install —
Windows locks `Kagura.dll` while the game has it loaded.

`scripts\build-prism.ps1` pins a specific upstream Prism revision so releases are reproducible; pass `-Ref` to build a
different one.

---

## Releases

Pushing a tag builds and publishes automatically:

```
git tag v1.3
git push origin v1.3
```

The workflow in `.github/workflows/release.yml` builds Prism, builds Kagura, and attaches `kagura_v1.3.zip` to the
GitHub release.

---

## Debugging

Kagura writes everything to `kagura.log` in its mod folder, and reads commands from a `kagura.cmd` file dropped in the
same place — one command per line, executed and deleted within about a quarter of a second.

- `status` — report speech, hook and mapping state
- `names` — reload `names.txt` without restarting the game
- `log on` / `log off` — toggle logging of every resolved message
- `say <text>` — speak a literal string
- `msg <id>` — resolve a message ID and speak it
- `dump <prefix> [max]` — walk a prefix's numeric ID range and log every message that resolves
- `announce on` / `announce off` — same as `F8`
- `verbose on` / `verbose off` — include the description after the label
- `silence` — stop speech immediately

Logged messages are tagged `[msg]` when `names.txt` covers them and `[unmapped]` when it does not, which is how new
menus get mapped. `dump` is how a menu can be mapped without visiting it.

The framework also exposes a console with `kagura`, `ksay`, `kmsg` and `klog` commands if you prefer typing.

---

## A note on how this was built, and what to expect

Kagura was written with AI assistance, by a blind developer directing the work and testing every step in the actual
game. Everything described here has been tried and does work — speech, the menu announcements, the hotkeys, the release
build.

That is not the same as being thoroughly tested. It has been exercised by one person, with one screen reader (NVDA), on
one copy of the game at version 1.09. The other eleven Prism backends compile but have never been run. Large parts of
the game have never been visited, and any menu nobody has walked through yet will read out its raw description text
rather than a proper label. **Expect rough edges.**

If something reads badly, reads twice, or stays silent when it should not, that is a bug worth reporting rather than
something you have to live with. Most such problems are fixed by editing `names.txt`, which needs no rebuild.

**Issues and pull requests are welcome.** Useful things to include in an issue:

- What screen was open and what you were doing
- What you heard, and what you expected to hear
- Your `kagura.log` if you can — it names every message the game resolved, and lines tagged `[unmapped]` are usually the
whole answer

## Compatibility

Built against **NS4 version 1.09**. Kagura verifies the game's instruction bytes before patching anything and refuses to
install its hooks if they do not match, so a future game update will disable the text hook rather than crash. If that
happens, `kagura.log` says so explicitly.

---

## About the name

**Kagura** comes from the *Kagura Shingan* — the Mind's Eye of the Kagura — a sensory technique that perceives chakra
across great distances and through solid obstacles, entirely without looking at anything. It is perception that never
depended on eyesight in the first place, which is exactly what this mod is: the game's state, reaching you by another
route.

**Byakugan** was the obvious candidate and was considered, but it is the wrong fit twice over. It is a *dojutsu* — an
eye technique, defined by sight — and it sees *everything*: three-hundred-and-sixty degrees, through walls, down to the
chakra network. That promises far more than this mod delivers. Kagura reads menus and text; it does not hand you
omniscient awareness of a battlefield.

A sensory technique that works without eyes is the honest metaphor. An all-seeing eye would have been a bigger claim
than the software can make.

## Licence

Kagura is released under the MIT Licence — see [LICENSE](LICENSE).

Kagura ships `prism.dll`, which is licensed under the Mozilla Public Licence 2.0. See [NOTICE](NOTICE) for details and
where to obtain its source.
