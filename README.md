# Greeneye Loader

A small mod loader for Enshrouded and its dedicated server. It loads the mods in the `greeneye` folder and lets them run side by side, including alongside other people's mods.

Current version: 1.2.2. Windows only. We don't test on Linux or Wine, and reports so far say it doesn't start there.

## Installing

Most players don't need this repository: each mod's download already includes the loader.

To install it by hand, put `dbgcore.dll` next to `enshrouded.exe` or `enshrouded_server.exe`, and each mod in `greeneye\<id>\`.

If another mod already uses the name `dbgcore.dll`, rename the loader to `umpdc.dll` or `cfgmgr32.dll`. If two copies end up in one process, the second stays idle.

## How it works

The loader is a proxy DLL. Every export of the Windows `dbgcore`, `umpdc` and `cfgmgr32` DLLs is forwarded to the real one, so the loader has no code in any call the game makes.

Loaded as `dbgcore.dll`, it starts the mods at the exe's entry point, before any game code runs and outside the loader lock. Loaded under the other two names it arrives later, so the mods start right away.

What it did, and why it skipped a mod, is written to `greeneye\loader.log`. Please include that file in a bug report. It starts fresh with each game or server start and rolls over at 10 MB: the earlier part moves to `loader.old.log`.

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
build.bat            -> bin\dbgcore.dll
tools\tests.bat      -> offline loader tests (needs build.bat first)
tools\hottests.bat   -> hot reload end-to-end test (needs build.bat first)
```

`tools\gen_exports.py` regenerates `src\exports.h`, which is only needed after a Windows update changes the exports of those three DLLs. Run it from a VS x64 developer prompt.

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
