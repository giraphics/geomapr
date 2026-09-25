# Map not visible — stale absolute DatabasePath

## Symptom

The Singapore map (roads, buildings, POIs) doesn't render in the level —
no error dialog, no failed compile, the level just opens empty and stays
empty in both editor and Play mode.

## Root cause

`MapViewActor`'s `DatabasePath` property is set once, by hand, in the
Details panel for the actor instance placed in `Content/Phase_6.umap`,
and gets baked into that level asset as an absolute path. On this
project it was baked in as:

```
C:\dev\giraphics\MobileNavigation\Final-Project\phase5_map\map_extended.db
```

`MobileNavigation` isn't this machine's actual folder name — the repo is
checked out at `C:\dev\giraphics\osm-navigation`, so `phase5_map` lives at
`osm-navigation\Final-Project\phase5_map\map_extended.db`. The stale path
doesn't exist here, so `FSQLiteDatabase::Open()` fails silently in
`InitRefOrigin()` — it only logs to the Output Log
(`MapViewActor: cannot open ...`), never surfaces on screen — and the
query/redraw path in `Tick()` never runs because `bRefInitialised` stays
false. The bare `Phase_6` project (the sibling folder without procedural
mesh buildings) has the exact same stale path baked into its own
`Content/Phase_6.umap` and would hit the same failure.

This is the same category of bug as `one-engine-many-worlds/ISSUES.md`'s
stale-sibling-path entries and the `pivots/racing-drone-crowdsim` port's
`MAP_DB`/`MAP_THEMES` fix: a hardcoded absolute path to a folder that
doesn't exist on whichever machine opens the project next.

## Fix

`MapViewActor::InitRefOrigin()` now calls a new `ResolveDatabasePath()`
first. If the configured `DatabasePath` doesn't exist on disk, it falls
back to the repo-relative location (`FPaths::ProjectDir()/../../Final-
Project/phase5_map/map_extended.db`, i.e. two directories up from
whichever machine's actual project folder, then into the fixed
`Final-Project/phase5_map/` path) and rewrites `DatabasePath` in place —
every later `Db.Open()` call in this file then picks up the corrected
path automatically. A warning is logged when the fallback kicks in, and
an error if even the fallback path is missing, so a genuinely broken
setup still fails loudly instead of just staying blank.

**This does not replace setting `DatabasePath` correctly in the Details
panel** — it's a safety net for when the repo moves, not a reason to
leave the property empty. To clear the warning, open `Phase_6.umap`,
select the `MapViewActor`, and set `DatabasePath` to the correct absolute
path on this machine:

```
C:\dev\giraphics\osm-navigation\Final-Project\phase5_map\map_extended.db
```

then save the level.
