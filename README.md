# Greeneye Loader

A small mod loader for Enshrouded and its dedicated server. It loads the mods in the `greeneye` folder and lets them run side by side, including alongside other people's mods.

Current version: 1.2.4. Made for Windows. On Linux (Proton or Wine) it runs under the name `dinput8.dll`: see [Linux](#linux).

## Installing

Most players don't need this repository: each mod's download already includes the loader.

To install it by hand, put `dbgcore.dll` next to `enshrouded.exe` or `enshrouded_server.exe`, and each mod in `greeneye\<id>\`.

If another mod already uses the name `dbgcore.dll`, rename the loader to `umpdc.dll` or `cfgmgr32.dll`. If two copies end up in one process, the second stays idle.

## Linux

Under Proton or Wine the name `dbgcore.dll` never loads: the game asks for `dbghelp.dll`, and Wine's own `dbghelp` doesn't pull in `dbgcore`. Both `enshrouded.exe` and `enshrouded_server.exe` import `dinput8.dll` directly, so on Linux:

1. Rename `dbgcore.dll` to `dinput8.dll`, next to the exe. It's the same file.
2. Tell Wine to load the folder's copy first, with `WINEDLLOVERRIDES="dinput8=n,b"`:
   - Steam (Proton) or Steam Deck: Properties > General > Launch Options: `WINEDLLOVERRIDES="dinput8=n,b" %command%`
   - Dedicated server under Wine: `export WINEDLLOVERRIDES="dinput8=n,b"` before starting `enshrouded_server.exe`.
3. Start the game or server. `greeneye\loader.log` should appear, and its first line ends with the Wine version, e.g. `on Wine 9.0`.

[`dist/linux/LINUX.txt`](dist/linux/LINUX.txt) is the player guide. It covers every place the override can go: Steam launch options (and how to combine them with existing ones), winecfg through protontricks, Lutris, plain Wine, server start scripts, Docker and server panels. Mods can ship it with [`dinput8-chain.ini.example`](dist/linux/dinput8-chain.ini.example) in `greeneye\linux\`.

As `dinput8.dll` the loader passes the game's DirectInput calls to the real `dinput8.dll` in the system folder. It finds that file by its full path, because Wine can't follow the forwarders the other three names use.

### Chain loading other dinput8 mods

Only one file can be `dinput8.dll`. Other mods that want the name (Creative Mode, XHL, ...) are renamed and listed in `dinput8-chain.ini` next to the exe. The loader reads that file by the shared spec that EMF, Kiln, ShroudForge, XHL-Core and Auto Loot Plus follow ([`docs/modding/dinput8-chain.md`](https://github.com/matenm/emf-enshrouded/blob/main/docs/modding/dinput8-chain.md) in EMF's repository). A player's list works whichever of those loaders holds `dinput8.dll`:

```ini
; my mods
[Chain]
creative.dll
mods\mymod\mymod.dll   ; a comment after a space or tab
; oldmod.dll            ; "; " in front switches a DLL off
```

- **Encoding:** UTF-8 with or without a BOM, or UTF-16 LE with a BOM. If the bytes aren't valid UTF-8, the ANSI code page.
- **Sections:** lines before any section count as `[Chain]`. Other sections belong to other tools and are ignored, with one exception (below).
- **Lines:** `;`, `#` and `//` start a comment line. Names may stand in double quotes. A `key = value` line counts when its value names a `.dll`; other keys are other tools' settings, such as XHL's `Enabled=` and `Forward=`.
- **Paths:** relative to the exe's folder, and `/` is read as `\`. Absolute paths, drive letters, UNC paths and any `..` part are refused. Only `.dll` files count.
- **Skipped:** the file itself, any copy of the Greeneye Loader, a DLL that is already loaded, and a DLL the crash guard holds back.
- **Duplicates and limit:** each DLL starts once, at its first place. At most 256.
- **When:** the chain loads at the exe's entry point, before the Greeneye mods and before any game code.
- **Logging:** each DLL gets a line in `loader.log`: started, skipped, refused, or failed with Windows' error number.
- **DirectInput:** the game's `DirectInput8Create` always goes to Windows' own `dinput8.dll`. Chained DLLs get no DirectInput calls, as the spec says.
- **One exception to the spec:** like Kiln, the loader also reads `[EMF]`. EMF's installers move other tools' `dinput8.dll` (e.g. Creative Mode) into that section, and a player switching from EMF would otherwise lose them.
- **Who reads the file:** only the game's own `dinput8.dll`, meaning the copy in the game folder that the process knows by that name. A copy named `dinput8.dll` that another loader loads from somewhere else leaves the file to that loader. Under any other name the loader ignores the file.
- **How copies are recognized:** every version of the loader carries the `ProductName` "Greeneye Loader" in its version info. If that's missing, the loader checks for its own log text and forwarder path in the file.

### Two copies in one folder

`dbgcore.dll` and `dinput8.dll` can both be the loader. That happens on Windows after following the Linux guide and then installing a mod zip that brings `dbgcore.dll` back.

- Both load. The **newest** copy runs the Greeneye mods (since 1.2.4):
  - every copy loaded at process start writes its version to a small shared memory block in its DllMain;
  - they decide at the exe's entry point, once all of them have;
  - on equal versions, the first to get there wins.
- The copy that runs the mods records `GREENEYE_LOADER=<version> as <name>` in the process environment.
- The other writes `greeneye\loader.second-copy.log`, naming the copy that runs the mods. If it is itself the newer version, it says so.
- Copies before 1.2.4 don't take part: they claim the mods in their DllMain.
- If the `dinput8.dll` copy is the idle one, it still loads `dinput8-chain.ini` and logs that to `greeneye\loader.chain.log`.
- On Linux the `dbgcore.dll` copy never loads.

### Loaded by another loader

Someone else's chain loader (XHL's `dinput8-chain.ini`, an ASI loader) can load the Greeneye Loader under another name, like `greeneye-loader.dll`. That's a late load, the same path as `umpdc.dll` and `cfgmgr32.dll`: the mods start right away, inside that loader's load call.

## How it works

The loader is a proxy DLL. Every export of the Windows `dbgcore`, `umpdc` and `cfgmgr32` DLLs is forwarded to the real one, so the loader has no code in any call the game makes.

Loaded as `dbgcore.dll`, it starts the mods at the exe's entry point, before any game code runs and outside the loader lock. Loaded under the other two names it arrives later, so the mods start right away.

What it did, and why it skipped a mod, is written to `greeneye\loader.log`. Please include that file in a bug report. It starts fresh with each game or server start and rolls over at 10 MB: the earlier part moves to `loader.old.log`.

### Crash guard

A mod, or a DLL in `dinput8-chain.ini`, that takes the whole game down while it starts is skipped at the next launches. It stays skipped until its DLL changes (an update). To try everything again, delete `greeneye\crashguard.txt`.

- **How it detects a crash:** before each start, the loader writes `greeneye\loader.starting` naming what is starting, and deletes it once the start returns. If the file is still there at the next launch, the process died during that start.
- **What it covers:** a fault inside `GreeneyeModStart` is caught already and doesn't kill the game. The guard catches what can't be caught that way: a crash in a mod's DllMain, a fast-fail, or a player ending a hung start.
- **What it can't cover:** only the start itself is watched. A crash later in play can't be pinned on one mod this way.

### hooks.txt

After the mods start, `greeneye\hooks.txt` lists every place in the game's code the loader patched, with the mod that owns each.

- The loader compares each place with `enshrouded.exe` on disk, ignoring bytes the OS relocated.
- If the bytes there weren't the game's own when it hooked, the line says "patched before us". When it can, the line also says which module the other patch goes into, e.g. `(goes into creative.dll)`.
- `loader.log` gets a one-line summary. Hook sites added later by a hot reload aren't listed.

## Writing a mod

A mod is a DLL with a `mod.json` beside it:

```json
{"id": "my-mod", "name": "My Mod", "version": "1.0", "dll": "my-mod.dll", "side": "client", "api": 2}
```

| Field | Meaning | Default |
|---|---|---|
| `id` | The mod's id | The folder's name |
| `name` | Name shown in the log | The id |
| `version` | Version shown in the log | `?` |
| `dll` | The DLL to load | `<id>.dll` |
| `side` | `client`, `server` or `both` | `client` |
| `api` | The API version the mod needs; a newer one than the loader knows is skipped | `1` |
| `enabled` | `false` skips the mod | `true` |

The DLL exports `GreeneyeModStart`. The API is in [`src/greeneye.h`](src/greeneye.h):

```cpp
#include "greeneye.h"

GREENEYE_MOD_START
{
    greeneye::UseHost(host);
    host->log("my mod started");
    return true;
}
```

Under API 2, the loader owns every hook:

- Install hooks with `host->hook` and `host->redirectCall`. Never patch the game yourself.
- Scan with `host->find` and `host->read`. They see the code as it was before any of the loader's hooks, and `find` also matches behind another mod's hook.
- Hold a `greeneye::Lease` in every detour while it runs your code. If the lease is inactive, you're in an older generation: call on to the original and do nothing else.

`greeneye::LoaderName(host)` says which loader runs the mod, e.g. "Greeneye Loader 1.2.4 as dbgcore.dll", for your own log file or a support report.

- From 1.2.4 the loader names itself in `GreeneyeHost::loader` and reports API 3. That's the only change in API 3, so mods keep `"api": 2` in `mod.json` and still load under API 2 hosts.
- Read the field only through `LoaderName` or `HostNamesLoader`, which require both `apiVersion >= 3` and a large enough `size`. Another host built from a copy of the API 2 header may have appended fields of its own at that spot.
- Older loaders, and other hosts that load Greeneye mods (such as Kiln's adapter), don't fill it in.
- In that case `greeneye::UseHost` writes one line in the host's log from each mod's first start: "(this mod runs under a loader that doesn't name itself ...)". A log from another host shows that at once.

`HostHasHooks` only requires the API 2 fields, so mods built with the newer header still hook under older loaders.

## Hot reload

For mod developers, and off unless `greeneye\hotreload.enable` exists. Players get the same loader with it dormant.

1. Copy the new build over `greeneye\<id>\<id>.dll` while the game runs. Each generation loads from a shadow copy in `greeneye\<id>\.hot\`, so the file isn't locked.
2. Create the file `greeneye\<id>\reload`.
3. Read `greeneye\<id>\hotload.status`. The reload has worked only when it says `active generation N` for the new one.

The swap happens at a safe point, once every lease is released: in the game at the top of a UI frame, on the server on the loader's watcher thread. If the new generation fails to start, the old one keeps running.

A reload may add new hook sites (since 1.2.0). State that must outlive a generation goes in `host->keep(key, size)`. A mod can also export `GreeneyeModHot` to put away its threads and windows before a swap.

## Building

Needs the Visual Studio 2022 Build Tools (C++ x64).

```
build.bat            -> bin\dbgcore.dll (and the same file as bin\dinput8.dll)
tools\tests.bat      -> offline loader tests, including the dinput8 name (needs build.bat first)
tools\hottests.bat   -> hot reload end-to-end test (needs build.bat first)
```

`tools\gen_exports.py` regenerates `src\exports.h`, which is only needed after a Windows update changes the exports of those three DLLs. Run it from a VS x64 developer prompt.

Every mod zip ships `dist\linux\LINUX.txt` and `dist\linux\dinput8-chain.ini.example` in `greeneye\linux\`. Package scripts take them from `tools\linux_files.py` (`linux_files.entries()`). For a zip made by hand, run `python -I tools\linux_files.py <zip>` to add them.

`tools\smoke.bat` also tries the loader with real mods, but it expects their source folders beside this one, so it won't run from this repository alone.

`src\greeneye.h`, `src\jumppatch.h` and `src\async_log.h` are shared with the mods. Comments in them that mention `common/` refer to that shared folder, which isn't part of this repository.

## Mods that use it

- [The Worst Map Mod](https://github.com/Greeneye0/worst-map-mod-enshrouded)
- [Find](https://github.com/Greeneye0/find-enshrouded)
- [Quick Deposit](https://github.com/Greeneye0/enshrouded-quick-deposit)
- [Town Portals](https://github.com/Greeneye0/town-portals-enshrouded)
- [Embervale Teleport](https://github.com/Greeneye0/embervale-teleport-enshrouded)
- [Hollow Halls Progression](https://github.com/Greeneye0/hollow-halls-progression-enshrouded)

## License

MIT. See [LICENSE](LICENSE).
