# Editor Mode Rendering — Phase_6_procedural_mesh

By default, UE5 actors only run their logic when the Play button is pressed. These changes
make the map render live in the editor viewport without entering Play mode.

---

## Problem

Three things prevent an actor from working in the editor:

1. **No tick in editor** — UE5 does not tick actors in the editor viewport unless explicitly enabled.
2. **No BeginPlay** — `BeginPlay()` is never called in editor mode, so the database never
   initialises and no map data is ever queried.
3. **No PlayerController / Pawn** — The camera helpers (`GetCameraAltitudeM`,
   `GetCameraGeoBounds`) call `GetWorld()->GetFirstPlayerController()`, which returns
   `nullptr` in the editor. This causes the functions to return early with no valid position.

---

## Changes

### 1. `MapViewActor.h` — enable editor tick

Add `ShouldTickIfViewportOnly()` to the protected overrides:

```cpp
// BEFORE
protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

// AFTER
protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual bool ShouldTickIfViewportOnly() const override { return true; }
```

`ShouldTickIfViewportOnly()` returning `true` is the single switch that tells the UE5
engine to tick this actor even when no game is running.

---

### 2. `MapViewActor.cpp` — initialise from `Tick` in editor mode

`BeginPlay()` is never called in the editor, so its deferred `SetTimer` initialisation
never fires. Add a guard at the top of `Tick` that calls `InitRefOrigin()` directly on
the first editor tick:

```cpp
// BEFORE
void AMapViewActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!bRefInitialised) return;
    // ... query + redraw logic
}

// AFTER
void AMapViewActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Editor mode: BeginPlay never fires, so initialise here on first tick.
    if (!bRefInitialised && !DatabasePath.IsEmpty())
    {
        if (InitRefOrigin())
        {
            bNeedsRefresh    = true;
            bBldNeedsRefresh = true;
            bPoiNeedsRefresh = true;
        }
        return;  // next tick will do the first query
    }

    if (!bRefInitialised) return;
    // ... query + redraw logic (unchanged)
}
```

---

### 3. `MapViewActor.cpp` — `GetCameraAltitudeM` editor fallback

Return the actor's own Z (or a minimum of 1500 m) when no pawn exists:

```cpp
// BEFORE
float AMapViewActor::GetCameraAltitudeM() const
{
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (!PC || !PC->GetPawn()) return 500.f;
    return FMath::Max(PC->GetPawn()->GetActorLocation().Z / 100.f, 0.f);
}

// AFTER
float AMapViewActor::GetCameraAltitudeM() const
{
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (PC && PC->GetPawn())
        return FMath::Max(PC->GetPawn()->GetActorLocation().Z / 100.f, 0.f);
    // Editor mode: use the actor's own Z as altitude.
    // Default 1500 m ensures roads + buildings are within the visibility threshold.
    return FMath::Max(GetActorLocation().Z / 100.f, 1500.f);
}
```

The 1500 m default keeps altitude below `ShowBuildingsAltM` (2000 m) so buildings
are included in the first editor query.

---

### 4. `MapViewActor.cpp` — `GetCameraGeoBounds` editor fallback

Use the actor's world location as the map centre when no pawn exists:

```cpp
// BEFORE
APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
if (!PC || !PC->GetPawn())
{
    OutMinLon = RefLon - 0.01; OutMaxLon = RefLon + 0.01;
    OutMinLat = RefLat - 0.01; OutMaxLat = RefLat + 0.01;
    return;
}
const FVector Pos = PC->GetPawn()->GetActorLocation();

// AFTER
APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
// Editor mode: no pawn — centre the query on the actor's placement.
const FVector Pos = (PC && PC->GetPawn())
    ? PC->GetPawn()->GetActorLocation()
    : GetActorLocation();
```

The rest of the function is unchanged — `Pos` drives the geo-bounds calculation
the same way in both modes.

---

## Behaviour after the fix

| Situation | Init path | Camera centre | Altitude |
|---|---|---|---|
| **Play mode** | `BeginPlay()` timer | Pawn location | Pawn Z ÷ 100 |
| **Editor mode** | First `Tick()` call | Actor location | max(Actor Z ÷ 100, 1500 m) |

**Usage in the editor:**

1. Place the `MapViewActor` in the level at `(0, 0, 0)` (or wherever you want the map
   origin).
2. Set `DatabasePath` in the Details panel to
   `…/phase5_map/map_extended.db`.
3. The map appears in the viewport immediately — no Play required.
4. Moving the actor laterally pans the map centre.
