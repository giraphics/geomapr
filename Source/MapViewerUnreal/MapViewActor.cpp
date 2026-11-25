#include "MapViewActor.h"

#include "SQLiteDatabase.h"
#include "SQLitePreparedStatement.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "KismetProceduralMeshLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

// ---------------------------------------------------------------------------
// Road type → colour
// ---------------------------------------------------------------------------
FColor AMapViewActor::RoadTypeColor(const FString& Type)
{
    if (Type == TEXT("motorway"))      return FColor(255,  61,  61);
    if (Type == TEXT("trunk"))         return FColor(255, 140,  51);
    if (Type == TEXT("primary"))       return FColor(255, 219,   0);
    if (Type == TEXT("secondary"))     return FColor(199, 199,   0);
    if (Type == TEXT("tertiary"))      return FColor(179, 179, 179);
    if (Type == TEXT("residential"))   return FColor(161, 161, 161);
    if (Type == TEXT("service"))       return FColor(120, 120, 120);
    if (Type == TEXT("footway"))       return FColor( 79, 199,  79);
    if (Type == TEXT("path"))          return FColor( 79, 199,  79);
    if (Type == TEXT("cycleway"))      return FColor( 61, 140, 219);
    if (Type == TEXT("living_street")) return FColor(140, 181, 140);
    if (Type == TEXT("steps"))         return FColor(161, 120,  79);
    return                                    FColor( 99,  99,  99);
}

// ---------------------------------------------------------------------------
// RoadWidthCm - ribbon width by highway type, in centimetres.
// Wider classes read first when zoomed out, which is what gives the map its
// visual hierarchy. Footpaths stay thin so they never dominate.
// ---------------------------------------------------------------------------
float AMapViewActor::RoadWidthCm(const FString& Type)
{
    if (Type == TEXT("footway") || Type == TEXT("path") ||
        Type == TEXT("steps")   || Type == TEXT("cycleway"))  return 200.f;
    if (Type.StartsWith(TEXT("motorway")))                    return 1200.f;
    if (Type.StartsWith(TEXT("trunk")))                       return 1000.f;
    if (Type.StartsWith(TEXT("primary")))                     return 900.f;
    if (Type.StartsWith(TEXT("secondary")))                   return 800.f;
    if (Type.StartsWith(TEXT("tertiary")))                    return 700.f;
    if (Type == TEXT("residential"))                          return 600.f;
    if (Type == TEXT("living_street"))                        return 500.f;
    if (Type == TEXT("unclassified"))                         return 500.f;
    if (Type == TEXT("service"))                              return 400.f;
    if (Type == TEXT("track"))                                return 300.f;
    return                                                           450.f;
}

// ---------------------------------------------------------------------------
// RoadClassOf - the tiers IsTypeVisible encodes, as a number.
// A section of class C is visible when C <= LodLevel(altitude).
// ---------------------------------------------------------------------------
int32 AMapViewActor::RoadClassOf(const FString& RawType)
{
    // A "_link" road is a ramp or slip road of the type before it, so
    // "secondary_link" ranks with "secondary" rather than falling to class 3.
    const FString Type = RawType.EndsWith(TEXT("_link")) ? RawType.LeftChop(5) : RawType;

    if (Type == TEXT("motorway") || Type == TEXT("trunk")) return 0;
    if (Type == TEXT("primary")  || Type == TEXT("secondary")) return 1;
    if (Type == TEXT("tertiary")     || Type == TEXT("residential") ||
        Type == TEXT("unclassified") || Type == TEXT("living_street")) return 2;
    return 3;
}

// ---------------------------------------------------------------------------
// ApplyRoadLod - show/hide whole sections. No geometry is rebuilt, so zooming
// costs a handful of visibility flags rather than a full mesh regeneration.
// ---------------------------------------------------------------------------
void AMapViewActor::ApplyRoadLod(float AltM)
{
    const int32 Lod = LodLevel(AltM);
    int32 Visible = 0;
    for (int32 i = 0; i < RoadSectionClass.Num(); ++i)
    {
        const bool bOn = (RoadSectionClass[i] <= Lod);
        RoadMesh->SetMeshSectionVisible(i, bOn);
        if (bOn) ++Visible;
    }
    DrawnSegmentCount = Visible;
}

// ---------------------------------------------------------------------------
// Ground-level feature detector.
// OSM landuse/leisure/amenity polygons (parks, parking lots, pitches, etc.)
// sometimes appear in the buildings table.  These should be rendered as thin
// 2 m slabs, not as full-height building boxes.
// ---------------------------------------------------------------------------
static bool IsGroundFeature(const FString& Type)
{
    // Parking / transport
    if (Type == TEXT("parking"))          return true;
    if (Type == TEXT("parking_lot"))      return true;
    if (Type == TEXT("bicycle_parking"))  return true;
    // Green / nature
    if (Type == TEXT("park"))             return true;
    if (Type == TEXT("garden"))           return true;
    if (Type == TEXT("grass"))            return true;
    if (Type == TEXT("meadow"))           return true;
    if (Type == TEXT("forest"))           return true;
    if (Type == TEXT("wood"))             return true;
    if (Type == TEXT("scrub"))            return true;
    if (Type == TEXT("heath"))            return true;
    if (Type == TEXT("allotments"))       return true;
    if (Type == TEXT("orchard"))          return true;
    if (Type == TEXT("vineyard"))         return true;
    // Sports / recreation
    if (Type == TEXT("pitch"))            return true;
    if (Type == TEXT("playground"))       return true;
    if (Type == TEXT("sports_centre"))    return true;
    if (Type == TEXT("track"))            return true;
    // Agriculture
    if (Type == TEXT("farmland"))         return true;
    if (Type == TEXT("farmyard"))         return true;
    if (Type == TEXT("farm"))             return true;
    if (Type == TEXT("greenhouse_horticulture")) return true;
    // Water / special
    if (Type == TEXT("water"))            return true;
    if (Type == TEXT("reservoir"))        return true;
    if (Type == TEXT("basin"))            return true;
    if (Type == TEXT("beach"))            return true;
    if (Type == TEXT("sand"))             return true;
    // Other non-building
    if (Type == TEXT("cemetery"))         return true;
    if (Type == TEXT("construction"))     return true;
    if (Type == TEXT("brownfield"))       return true;
    if (Type == TEXT("greenfield"))       return true;
    if (Type == TEXT("quarry"))           return true;
    if (Type == TEXT("landfill"))         return true;
    return false;
}

// ---------------------------------------------------------------------------
// Building / ground-feature type → linear colour
// Ground features use distinct saturated colours so they read separately from
// the grey/blue/purple building palette.
// ---------------------------------------------------------------------------
FLinearColor AMapViewActor::BuildingColor(const FString& Type)
{
    auto srgb = [](uint8 r, uint8 g, uint8 b) {
        return FLinearColor(r / 255.f, g / 255.f, b / 255.f);
    };

    // ── Actual buildings ───────────────────────────────────────────────────
    if (Type == TEXT("apartments"))  return srgb(180, 120, 200);
    if (Type == TEXT("hospital"))    return srgb(255, 100, 100);
    if (Type == TEXT("commercial"))  return srgb(100, 180, 255);
    if (Type == TEXT("retail"))      return srgb( 80, 160, 255);
    if (Type == TEXT("school"))      return srgb(255, 200,  80);
    if (Type == TEXT("university"))  return srgb(255, 220, 100);
    if (Type == TEXT("residential")) return srgb(200, 200, 200);
    if (Type == TEXT("house"))       return srgb(210, 210, 210);
    if (Type == TEXT("detached"))    return srgb(220, 220, 220);
    if (Type == TEXT("industrial"))  return srgb(160, 140, 120);
    if (Type == TEXT("garage"))      return srgb(170, 170, 165);
    if (Type == TEXT("garages"))     return srgb(170, 170, 165);
    if (Type == TEXT("shed"))        return srgb(180, 155, 120);
    if (Type == TEXT("warehouse"))   return srgb(150, 135, 115);

    // ── Ground-level features ─────────────────────────────────────────────
    // Parking
    if (Type == TEXT("parking") ||
        Type == TEXT("parking_lot") ||
        Type == TEXT("bicycle_parking"))  return srgb(120, 120, 135);  // asphalt grey-blue
    // Parks / green
    if (Type == TEXT("park") ||
        Type == TEXT("garden") ||
        Type == TEXT("grass") ||
        Type == TEXT("meadow"))           return srgb(120, 200, 100);  // bright green
    // Forest / scrub
    if (Type == TEXT("forest") ||
        Type == TEXT("wood") ||
        Type == TEXT("scrub") ||
        Type == TEXT("heath"))            return srgb( 60, 130,  60);  // dark green
    // Agriculture
    if (Type == TEXT("allotments") ||
        Type == TEXT("orchard") ||
        Type == TEXT("vineyard") ||
        Type == TEXT("farmland") ||
        Type == TEXT("farmyard") ||
        Type == TEXT("farm") ||
        Type == TEXT("greenhouse_horticulture")) return srgb(190, 200, 120); // straw-green
    // Sports
    if (Type == TEXT("pitch") ||
        Type == TEXT("track"))            return srgb( 80, 170,  80);  // sports green
    if (Type == TEXT("playground"))       return srgb(255, 180,  80);  // amber
    if (Type == TEXT("sports_centre"))    return srgb( 80, 200, 160);  // teal
    // Water
    if (Type == TEXT("water") ||
        Type == TEXT("reservoir") ||
        Type == TEXT("basin"))            return srgb( 80, 150, 220);  // water blue
    // Sand / beach
    if (Type == TEXT("beach") ||
        Type == TEXT("sand"))             return srgb(225, 205, 145);  // sandy
    // Cemetery
    if (Type == TEXT("cemetery"))         return srgb(160, 175, 145);  // muted sage
    // Construction / industrial land
    if (Type == TEXT("construction") ||
        Type == TEXT("brownfield") ||
        Type == TEXT("landfill"))         return srgb(200, 140,  60);  // orange-brown
    if (Type == TEXT("greenfield"))       return srgb(170, 210, 140);  // pale green
    if (Type == TEXT("quarry"))           return srgb(180, 160, 140);  // stone

    return srgb(180, 180, 180);   // unknown building default
}

// ---------------------------------------------------------------------------
// Building type → approximate number of levels (floors).
//
// The buildings table currently has no 'levels' column — these values are
// OSM-informed approximations.  To use real OSM building:levels data, add a
// levels INTEGER column to the buildings table and fetch it in the SQL query,
// then pass it directly to BuildingHeightCm() instead of calling this function.
// ---------------------------------------------------------------------------
static int32 BuildingLevels(const FString& Type)
{
    if (Type == TEXT("apartments"))  return  8;   // 8 floors
    if (Type == TEXT("hospital"))    return  5;   // 5 floors
    if (Type == TEXT("commercial"))  return  4;   // 4 floors
    if (Type == TEXT("retail"))      return  3;   // 3 floors
    if (Type == TEXT("school"))      return  3;
    if (Type == TEXT("university"))  return  3;
    if (Type == TEXT("residential")) return  3;
    if (Type == TEXT("house"))       return  1;   // single storey
    if (Type == TEXT("detached"))    return  1;
    if (Type == TEXT("industrial"))  return  2;
    if (Type == TEXT("warehouse"))   return  3;
    if (Type == TEXT("garage"))      return  1;
    if (Type == TEXT("garages"))     return  1;
    if (Type == TEXT("shed"))        return  1;
    return 2;   // unknown building — assume 2 floors
}

// ---------------------------------------------------------------------------
// Building / ground-feature type → height in UE world cm.
//
// Formula (actual buildings):
//   height = 600 + (levels − 1) × 300  cm
//   → level 1 = 6 m, level 2 = 9 m, level 3 = 12 m, level 8 = 27 m, …
//   Every additional floor adds 3 m (300 cm) above the 6 m base.
//
// Ground-level features (parking, parks, pitches, …) are capped at 200 cm
// (2 m) — just enough to be visible without resembling a building.
//
// Pass Levels > 0 to use a real value from the database (when the schema
// is extended to include a levels column); pass 0 to use the type-based
// approximation from BuildingLevels().
// ---------------------------------------------------------------------------
float AMapViewActor::BuildingHeightCm(const FString& Type, int32 Levels)
{
    if (IsGroundFeature(Type)) return 200.f;   // 2 m flat slab

    const int32 L = (Levels > 0) ? Levels : BuildingLevels(Type);
    return 600.f + FMath::Max(L - 1, 0) * 300.f;
    // level 1 → 600 cm (6 m)
    // level 2 → 900 cm (9 m)
    // level 3 → 1200 cm (12 m)
    // level 8 → 2700 cm (27 m)
}

// ---------------------------------------------------------------------------
// LodLevel — inverse of OpenGL zoom: higher altitude = more zoomed out.
// ---------------------------------------------------------------------------
int32 AMapViewActor::LodLevel(float AltM) const
{
    if (AltM > Lod0AltM) return 0;
    if (AltM > Lod1AltM) return 1;
    if (AltM > Lod2AltM) return 2;
    return 3;
}

// ---------------------------------------------------------------------------
// IsTypeVisible — mirrors phase4 MapRenderer::isTypeVisible()
// ---------------------------------------------------------------------------
bool AMapViewActor::IsTypeVisible(const FString& Type, float AltM) const
{
    const int32 Lod = LodLevel(AltM);
    if (Lod >= 3) return true;
    if (Type == TEXT("motorway") || Type == TEXT("trunk")) return true;
    if (Lod >= 1 && (Type == TEXT("primary") || Type == TEXT("secondary"))) return true;
    if (Lod >= 2 && (Type == TEXT("tertiary")    ||
                     Type == TEXT("residential")  ||
                     Type == TEXT("unclassified") ||
                     Type == TEXT("living_street"))) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Constructor — create components and load the base material
// ---------------------------------------------------------------------------
AMapViewActor::AMapViewActor()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    BuildingMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BuildingMesh"));
    BuildingMesh->SetupAttachment(RootComponent);
    BuildingMesh->bUseAsyncCooking = false;   // synchronous so mesh is ready immediately

    // Roads: flat coloured ribbons on the ground. Flat things need no shadow.
    RoadMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RoadMesh"));
    RoadMesh->SetupAttachment(RootComponent);
    RoadMesh->bUseAsyncCooking = false;
    RoadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RoadMesh->SetCanEverAffectNavigation(false);
    RoadMesh->SetCastShadow(false);

    // Ground: one large quad so the map never reads against a black void.
    GroundMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GroundMesh"));
    GroundMesh->SetupAttachment(RootComponent);
    GroundMesh->bUseAsyncCooking = false;
    GroundMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GroundMesh->SetCanEverAffectNavigation(false);
    GroundMesh->SetCastShadow(false);

    // Buildings are the only things with volume, so they alone cast shadows.
    BuildingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BuildingMesh->SetCanEverAffectNavigation(false);
    BuildingMesh->SetCastShadow(true);

    // BasicShapeMaterial exposes a "Color" vector parameter used by
    // UMaterialInstanceDynamic::SetVectorParameterValue at runtime.
    static ConstructorHelpers::FObjectFinder<UMaterial> MatFinder(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
    if (MatFinder.Succeeded())
        BuildingBaseMaterial = MatFinder.Object;
}

// ---------------------------------------------------------------------------
// BeginPlay — deferred init
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// OnConstruction — fires in the editor whenever the actor is placed or a
// property is changed in the Details panel.  This is the reliable editor-mode
// entry point; BeginPlay and Tick are not guaranteed to fire before Play.
// ---------------------------------------------------------------------------
void AMapViewActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    EnsureDatabasePath();
    if (DatabasePath.IsEmpty()) return;

    // Re-initialise from scratch so a DatabasePath change takes effect immediately.
    bRefInitialised  = false;
    bNeedsRefresh    = true;
    bBldNeedsRefresh = true;
    bPoiNeedsRefresh = true;

    // Components added to this class after the actor was saved into the level
    // can come back unregistered: invisible, while older components still draw.
    if (RoadMesh   && !RoadMesh->IsRegistered())   RoadMesh->RegisterComponent();
    if (GroundMesh && !GroundMesh->IsRegistered()) GroundMesh->RegisterComponent();

    if (!InitRefOrigin()) return;

    // Synchronous first load — query all three layers and redraw right now.
    const float AltM = GetCameraAltitudeM();
    double MinLon, MaxLon, MinLat, MaxLat;
    GetCameraGeoBounds(MinLon, MaxLon, MinLat, MaxLat);

    const double LonSpan = MaxLon - MinLon;
    const double LatSpan = MaxLat - MinLat;
    const double EMinLon = MinLon - LonSpan * CacheMargin;
    const double EMaxLon = MaxLon + LonSpan * CacheMargin;
    const double EMinLat = MinLat - LatSpan * CacheMargin;
    const double EMaxLat = MaxLat + LatSpan * CacheMargin;

    CacheMinLon = EMinLon; CacheMaxLon = EMaxLon;
    CacheMinLat = EMinLat; CacheMaxLat = EMaxLat;
    QueryAndStore(EMinLon, EMaxLon, EMinLat, EMaxLat);

    BldCacheMinLon = EMinLon; BldCacheMaxLon = EMaxLon;
    BldCacheMinLat = EMinLat; BldCacheMaxLat = EMaxLat;
    QueryAndStoreBuildings(EMinLon, EMaxLon, EMinLat, EMaxLat);

    PoiCacheMinLon = EMinLon; PoiCacheMaxLon = EMaxLon;
    PoiCacheMinLat = EMinLat; PoiCacheMaxLat = EMaxLat;
    QueryAndStorePOIs(EMinLon, EMaxLon, EMinLat, EMaxLat);

    bNeedsRefresh    = false;
    bBldNeedsRefresh = false;
    bPoiNeedsRefresh = false;

    RedrawAll(AltM);

    UE_LOG(LogTemp, Log,
        TEXT("MapViewActor OnConstruction: roads=%d bld=%d poi=%d alt=%.0fm"),
        CachedRows.Num(), BuildingCount, PoiCount, (double)AltM);
}

// ---------------------------------------------------------------------------
void AMapViewActor::BeginPlay()
{
    Super::BeginPlay();

    EnsureDatabasePath();
    if (DatabasePath.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("MapViewActor: DatabasePath not set."));
        return;
    }

    FTimerHandle InitTimer;
    GetWorldTimerManager().SetTimer(InitTimer, [this]()
    {
        if (InitRefOrigin())
        {
            bNeedsRefresh    = true;
            bBldNeedsRefresh = true;
            bPoiNeedsRefresh = true;
            UE_LOG(LogTemp, Log,
                TEXT("MapViewActor Phase6_PM: ref origin (%.6f, %.6f)."),
                RefLat, RefLon);
        }
    }, 0.3f, false);
}

// ---------------------------------------------------------------------------
// Tick — three-layer dirty-flag check
// Also handles editor-mode initialization (BeginPlay is not called in editor).
// ---------------------------------------------------------------------------
void AMapViewActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Editor mode: BeginPlay never fires, so initialize here on first tick.
    if (!bRefInitialised) EnsureDatabasePath();
    if (!bRefInitialised && !DatabasePath.IsEmpty())
    {
        if (InitRefOrigin())
        {
            bNeedsRefresh    = true;
            bBldNeedsRefresh = true;
            bPoiNeedsRefresh = true;
            UE_LOG(LogTemp, Log,
                TEXT("MapViewActor Phase6_PM (editor): ref origin (%.6f, %.6f)."),
                RefLat, RefLon);
        }
        return;  // next tick will do the first query
    }

    if (!bRefInitialised) return;

    TimeSinceLastCheck += DeltaTime;
    if (TimeSinceLastCheck < CheckIntervalSeconds && !bNeedsRefresh) return;
    TimeSinceLastCheck = 0.f;

    double VMinLon, VMaxLon, VMinLat, VMaxLat;
    GetCameraGeoBounds(VMinLon, VMaxLon, VMinLat, VMaxLat);
    const float CurrentAlt = GetCameraAltitudeM();

    // ── Roads ──────────────────────────────────────────────────────────────
    const bool bRoadsOutside = bNeedsRefresh        ||
                               VMinLon < CacheMinLon ||
                               VMaxLon > CacheMaxLon ||
                               VMinLat < CacheMinLat ||
                               VMaxLat > CacheMaxLat;
    const bool bLodChanged = (LodLevel(CurrentAlt) != LodLevel(LastDrawAltitudeM));

    // ── Buildings ──────────────────────────────────────────────────────────
    const bool bBldVisible          = (CurrentAlt <= ShowBuildingsAltM);
    const bool bBldThresholdCrossed = (bBldVisible != (LastDrawAltitudeM <= ShowBuildingsAltM));
    const bool bBldOutside = bBldNeedsRefresh         ||
                             VMinLon < BldCacheMinLon  ||
                             VMaxLon > BldCacheMaxLon  ||
                             VMinLat < BldCacheMinLat  ||
                             VMaxLat > BldCacheMaxLat;

    // ── POIs ───────────────────────────────────────────────────────────────
    const bool bPoiVisible          = (CurrentAlt <= ShowPoisAltM);
    const bool bPoiThresholdCrossed = (bPoiVisible != (LastDrawAltitudeM <= ShowPoisAltM));
    const bool bPoiOutside = bPoiNeedsRefresh         ||
                             VMinLon < PoiCacheMinLon  ||
                             VMaxLon > PoiCacheMaxLon  ||
                             VMinLat < PoiCacheMinLat  ||
                             VMaxLat > PoiCacheMaxLat;

    bool bAnyDirty = false;
    bool bLodOnly  = false;

    // ── Re-query dirty layers ───────────────────────────────────────────────

    if (bRoadsOutside)
    {
        const double LonSpan = VMaxLon - VMinLon;
        const double LatSpan = VMaxLat - VMinLat;
        CacheMinLon = VMinLon - LonSpan * CacheMargin;
        CacheMaxLon = VMaxLon + LonSpan * CacheMargin;
        CacheMinLat = VMinLat - LatSpan * CacheMargin;
        CacheMaxLat = VMaxLat + LatSpan * CacheMargin;

        const double T0 = FPlatformTime::Seconds();
        QueryAndStore(CacheMinLon, CacheMaxLon, CacheMinLat, CacheMaxLat);
        UE_LOG(LogTemp, Log,
            TEXT("Phase6_PM: road query #%d  %.1f ms  lod=%d  alt=%.0f m"),
            QueryCount + 1, (FPlatformTime::Seconds() - T0) * 1000.0,
            LodLevel(CurrentAlt), (double)CurrentAlt);
        LastQueryAltitudeM = CurrentAlt;
        bNeedsRefresh = false;
        ++QueryCount;
        bAnyDirty = true;
    }
    else if (bLodChanged)
    {
        bLodOnly = true;
    }

    if (bBldVisible && (bBldOutside || bBldThresholdCrossed))
    {
        const double LonSpan = VMaxLon - VMinLon;
        const double LatSpan = VMaxLat - VMinLat;
        BldCacheMinLon = VMinLon - LonSpan * CacheMargin;
        BldCacheMaxLon = VMaxLon + LonSpan * CacheMargin;
        BldCacheMinLat = VMinLat - LatSpan * CacheMargin;
        BldCacheMaxLat = VMaxLat + LatSpan * CacheMargin;

        const double T0 = FPlatformTime::Seconds();
        QueryAndStoreBuildings(BldCacheMinLon, BldCacheMaxLon, BldCacheMinLat, BldCacheMaxLat);
        UE_LOG(LogTemp, Log,
            TEXT("Phase6_PM: building query  %.1f ms  %d buildings  alt=%.0f m"),
            (FPlatformTime::Seconds() - T0) * 1000.0, BuildingCount, (double)CurrentAlt);
        bBldNeedsRefresh = false;
        bAnyDirty = true;
    }
    else if (bBldThresholdCrossed && !bBldVisible)
    {
        CachedBuildings.Reset();
        BuildingCount = 0;
        ClearBuildingMesh();
        bAnyDirty = true;
    }

    if (bPoiVisible && (bPoiOutside || bPoiThresholdCrossed))
    {
        const double LonSpan = VMaxLon - VMinLon;
        const double LatSpan = VMaxLat - VMinLat;
        PoiCacheMinLon = VMinLon - LonSpan * CacheMargin;
        PoiCacheMaxLon = VMaxLon + LonSpan * CacheMargin;
        PoiCacheMinLat = VMinLat - LatSpan * CacheMargin;
        PoiCacheMaxLat = VMaxLat + LatSpan * CacheMargin;

        const double T0 = FPlatformTime::Seconds();
        QueryAndStorePOIs(PoiCacheMinLon, PoiCacheMaxLon, PoiCacheMinLat, PoiCacheMaxLat);
        UE_LOG(LogTemp, Log,
            TEXT("Phase6_PM: POI query  %.1f ms  %d POIs  alt=%.0f m"),
            (FPlatformTime::Seconds() - T0) * 1000.0, PoiCount, (double)CurrentAlt);
        bPoiNeedsRefresh = false;
        bAnyDirty = true;
    }
    else if (bPoiThresholdCrossed && !bPoiVisible)
    {
        CachedPOIs.Reset();
        PoiCount = 0;
        bAnyDirty = true;
    }

    if (!bAnyDirty && bLodOnly)
    {
        const double T0 = FPlatformTime::Seconds();
        ApplyRoadLod(CurrentAlt);
        UE_LOG(LogTemp, Log, TEXT("Phase6_PM: lod-only  %.2f ms  lod=%d  alt=%.0f m"),
            (FPlatformTime::Seconds() - T0) * 1000.0, LodLevel(CurrentAlt), (double)CurrentAlt);
        LastDrawAltitudeM = CurrentAlt;
    }

    if (bAnyDirty)
    {
        const double T0 = FPlatformTime::Seconds();
        RedrawAll(CurrentAlt);
        UE_LOG(LogTemp, Log,
            TEXT("Phase6_PM: redraw  %.1f ms  roads %d/%d  bld %d  poi %d  lod=%d  alt=%.0f m"),
            (FPlatformTime::Seconds() - T0) * 1000.0,
            DrawnSegmentCount, TotalSegmentCount,
            BuildingCount, PoiCount,
            LodLevel(CurrentAlt), (double)CurrentAlt);
        LastDrawAltitudeM = CurrentAlt;
    }
}

// ---------------------------------------------------------------------------
// EndPlay
// ---------------------------------------------------------------------------
void AMapViewActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    FlushPersistentDebugLines(GetWorld());
    Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// EnsureDatabasePath
// ---------------------------------------------------------------------------
void AMapViewActor::EnsureDatabasePath()
{
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();

    if (!DatabasePath.IsEmpty() && PF.FileExists(*DatabasePath))
        return;                                  // already usable

    const FString Shipped = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectDir() / TEXT("Data/map_extended.db"));

    if (PF.FileExists(*Shipped))
    {
        if (DatabasePath.IsEmpty())
        {
            UE_LOG(LogTemp, Log,
                TEXT("MapViewActor: DatabasePath unset, using project database %s"),
                *Shipped);
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("MapViewActor: DatabasePath %s does not exist; ")
                TEXT("falling back to project database %s"),
                *DatabasePath, *Shipped);
        }
        DatabasePath = Shipped;
    }
    else
    {
        UE_LOG(LogTemp, Error,
            TEXT("MapViewActor: no usable database. DatabasePath=%s and the ")
            TEXT("project copy %s are both missing."),
            DatabasePath.IsEmpty() ? TEXT("(empty)") : *DatabasePath, *Shipped);
    }
}

// ---------------------------------------------------------------------------
// InitRefOrigin
// ---------------------------------------------------------------------------
bool AMapViewActor::InitRefOrigin()
{
    FSQLiteDatabase Db;
    if (!Db.Open(*DatabasePath, ESQLiteDatabaseOpenMode::ReadOnly))
    {
        UE_LOG(LogTemp, Error, TEXT("MapViewActor: cannot open %s"), *DatabasePath);
        return false;
    }

    bool bSuccess = false;
    {
        FSQLitePreparedStatement St(Db, TEXT("SELECT lat, lon FROM nodes LIMIT 1"));
        if (St.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            St.GetColumnValueByIndex(0, RefLat);
            St.GetColumnValueByIndex(1, RefLon);
            bSuccess = true;
        }
        // St destructs here — DB still open
    }
    Db.Close();
    if (!bSuccess) return false;

    const double DegToRad = 3.14159265358979323846 / 180.0;
    MetresPerDegLon = 111320.0 * FMath::Cos(RefLat * DegToRad);
    MetresPerDegLat = 110540.0;
    bRefInitialised = true;
    return true;
}

// ---------------------------------------------------------------------------
// GetCameraAltitudeM
// ---------------------------------------------------------------------------
float AMapViewActor::GetCameraAltitudeM() const
{
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (PC && PC->GetPawn())
        return FMath::Max(PC->GetPawn()->GetActorLocation().Z / 100.f, 0.f);
    // Editor mode: use the actor's own Z as altitude so the map area around
    // the actor placement is shown.  Default 1500 m shows roads + buildings.
    return FMath::Max(GetActorLocation().Z / 100.f, 1500.f);
}

// ---------------------------------------------------------------------------
// GetCameraGeoBounds
// ---------------------------------------------------------------------------
void AMapViewActor::GetCameraGeoBounds(
    double& OutMinLon, double& OutMaxLon,
    double& OutMinLat, double& OutMaxLat) const
{
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    // UE5: X = North (Lat), Y = East (Lon)
    double CenterLat, CenterLon, HalfExtentM;
    if (PC && PC->GetPawn())
    {
        const FVector Pos = PC->GetPawn()->GetActorLocation();
        CenterLat   = RefLat + Pos.X / (MetresPerDegLat * 100.0);
        CenterLon   = RefLon + Pos.Y / (MetresPerDegLon * 100.0);
        HalfExtentM = FMath::Max(Pos.Z / 100.0, 100.0);
    }
    else
    {
        // Editor mode: centre on actor position; use EditorViewRadiusM for extent
        // instead of Pos.Z (which is 0 when the actor sits at ground level).
        const FVector Pos = GetActorLocation();
        CenterLat   = RefLat + Pos.X / (MetresPerDegLat * 100.0);
        CenterLon   = RefLon + Pos.Y / (MetresPerDegLon * 100.0);
        HalfExtentM = FMath::Max((double)EditorViewRadiusM, 100.0);
    }
    const double LonHalfRange = HalfExtentM / MetresPerDegLon;
    const double LatHalfRange = HalfExtentM / MetresPerDegLat;

    OutMinLon = CenterLon - LonHalfRange;
    OutMaxLon = CenterLon + LonHalfRange;
    OutMinLat = CenterLat - LatHalfRange;
    OutMaxLat = CenterLat + LatHalfRange;
}

// ---------------------------------------------------------------------------
// QueryAndStore — road spatial query
// ---------------------------------------------------------------------------
void AMapViewActor::QueryAndStore(
    double MinLon, double MaxLon, double MinLat, double MaxLat)
{
    CachedRows.Reset();
    TotalSegmentCount = 0;

    FSQLiteDatabase Db;
    if (!Db.Open(*DatabasePath, ESQLiteDatabaseOpenMode::ReadOnly)) return;

    int32 PrevRoadId = -1, SegCount = 0;
    {
        FSQLitePreparedStatement St(Db,
            TEXT("SELECT r.road_id, r.highway_type, n.node_id, n.lat, n.lon, rn.sequence ")
            TEXT("FROM nodes_rtree AS idx ")
            TEXT("JOIN nodes      AS n  ON n.node_id  = idx.node_id ")
            TEXT("JOIN road_nodes AS rn ON rn.node_id = n.node_id ")
            TEXT("JOIN roads      AS r  ON r.road_id  = rn.road_id ")
            TEXT("WHERE idx.min_lon >= ? AND idx.max_lon <= ? ")
            TEXT("  AND idx.min_lat >= ? AND idx.max_lat <= ? ")
            TEXT("ORDER BY r.road_id, rn.sequence"));
        St.SetBindingValueByIndex(1, MinLon);
        St.SetBindingValueByIndex(2, MaxLon);
        St.SetBindingValueByIndex(3, MinLat);
        St.SetBindingValueByIndex(4, MaxLat);
        while (St.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            FRoadRow Row;
            St.GetColumnValueByIndex(0, Row.RoadId);
            St.GetColumnValueByIndex(1, Row.HighwayType);
            St.GetColumnValueByIndex(3, Row.Lat);
            St.GetColumnValueByIndex(4, Row.Lon);
            if (Row.RoadId == PrevRoadId) ++SegCount;
            PrevRoadId = Row.RoadId;
            CachedRows.Add(Row);
        }
    }
    Db.Close();
    TotalSegmentCount = SegCount;
}

// ---------------------------------------------------------------------------
// QueryAndStoreBuildings — OVERLAP bbox query
// ---------------------------------------------------------------------------
void AMapViewActor::QueryAndStoreBuildings(
    double MinLon, double MaxLon, double MinLat, double MaxLat)
{
    CachedBuildings.Reset();
    BuildingCount = 0;

    FSQLiteDatabase Db;
    if (!Db.Open(*DatabasePath, ESQLiteDatabaseOpenMode::ReadOnly)) return;

    int32 PrevBldId = -1;
    {
        FSQLitePreparedStatement St(Db,
            TEXT("SELECT b.building_id, b.building_type, bn.lat, bn.lon ")
            TEXT("FROM buildings_rtree AS idx ")
            TEXT("JOIN buildings       AS b  ON b.building_id  = idx.building_id ")
            TEXT("JOIN building_nodes  AS bn ON bn.building_id = b.building_id ")
            TEXT("WHERE idx.max_lon >= ? AND idx.min_lon <= ? ")
            TEXT("  AND idx.max_lat >= ? AND idx.min_lat <= ? ")
            TEXT("ORDER BY b.building_id, bn.sequence"));
        St.SetBindingValueByIndex(1, MinLon);
        St.SetBindingValueByIndex(2, MaxLon);
        St.SetBindingValueByIndex(3, MinLat);
        St.SetBindingValueByIndex(4, MaxLat);
        while (St.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            FBuildingRow Row;
            St.GetColumnValueByIndex(0, Row.BuildingId);
            St.GetColumnValueByIndex(1, Row.BuildingType);
            St.GetColumnValueByIndex(2, Row.Lat);
            St.GetColumnValueByIndex(3, Row.Lon);
            if (Row.BuildingId != PrevBldId) { ++BuildingCount; PrevBldId = Row.BuildingId; }
            CachedBuildings.Add(Row);
        }
    }
    Db.Close();
}

// ---------------------------------------------------------------------------
// QueryAndStorePOIs — bbox containment query
// ---------------------------------------------------------------------------
void AMapViewActor::QueryAndStorePOIs(
    double MinLon, double MaxLon, double MinLat, double MaxLat)
{
    CachedPOIs.Reset();
    PoiCount = 0;

    FSQLiteDatabase Db;
    if (!Db.Open(*DatabasePath, ESQLiteDatabaseOpenMode::ReadOnly)) return;

    {
        FSQLitePreparedStatement St(Db,
            TEXT("SELECT lat, lon, poi_type FROM pois ")
            TEXT("WHERE lon >= ? AND lon <= ? AND lat >= ? AND lat <= ?"));
        St.SetBindingValueByIndex(1, MinLon);
        St.SetBindingValueByIndex(2, MaxLon);
        St.SetBindingValueByIndex(3, MinLat);
        St.SetBindingValueByIndex(4, MaxLat);
        while (St.Step() == ESQLitePreparedStatementStepResult::Row)
        {
            FPoiRow Row;
            St.GetColumnValueByIndex(0, Row.Lat);
            St.GetColumnValueByIndex(1, Row.Lon);
            St.GetColumnValueByIndex(2, Row.PoiType);
            CachedPOIs.Add(Row);
            ++PoiCount;
        }
    }
    Db.Close();
}

// ---------------------------------------------------------------------------
// RebuildBuildingMesh — convert CachedBuildings into ProceduralMesh sections.
//
// Mirrors phase6_3d_world_cpp loadBuildings() geometry exactly:
//
//   Wall quad per polygon edge i → j=(i+1)%n (from z=0 to z=height):
//     v0=(wxi,wyi,0)  v1=(wxj,wyj,0)  v2=(wxj,wyj,h)  v3=(wxi,wyi,h)
//     Triangle A: {v0, v1, v2}   Triangle B: {v0, v2, v3}
//     Wall normal: perpendicular to edge in XY, pointing outward
//
//   Roof fan from nodes[0] at height h:
//     For k = 1 .. n-2: {nodes[0], nodes[k], nodes[k+1]}
//     Roof normal: (0, 0, 1) — pointing up
//
// Buildings are grouped by colour: all buildings sharing the same colour
// contribute geometry to one FBuildingSection, which becomes one mesh section.
// Each section gets its own UMaterialInstanceDynamic tinted to the group colour
// — one draw call per colour group, matching phase6's BuildingGroup pattern.
// ---------------------------------------------------------------------------
void AMapViewActor::RebuildBuildingMesh()
{
    BuildingMesh->ClearAllMeshSections();

    if (CachedBuildings.Num() == 0) return;

    // ProceduralMeshComponent vertices are in LOCAL space (actor-relative).
    // DrawDebugLine uses WORLD space with Z=0 at ground.
    // Subtract the actor's world Z so that local Z=0 → world Z=0 (ground).
    const float ActorZ = GetActorLocation().Z;

    // Map from colour key → FBuildingSection accumulator
    TMap<uint32, FBuildingSection> Groups;

    // Helper: colour → stable uint32 key
    auto ColorKey = [](const FLinearColor& C) -> uint32 {
        return (uint32(C.R * 255) << 16) | (uint32(C.G * 255) << 8) | uint32(C.B * 255);
    };

    // Process CachedBuildings — rows are ordered by building_id, sequence
    TArray<FVector> Nodes;   // (wx, wy) in UE cm for current building
    int32   CurBldId   = -1;
    FString CurBldType;

    // ── 2D ear-clipping triangulator ─────────────────────────────────────────
    // Fan triangulation from node[0] only works for convex polygons. OSM
    // building footprints are often concave/L-shaped; ear-clipping handles them.
    auto Triangulate2D = [](const TArray<FVector>& Poly, TArray<int32>& OutTris)
    {
        const int32 N = Poly.Num();
        if (N < 3) return;
        if (N == 3) { OutTris.Add(0); OutTris.Add(1); OutTris.Add(2); return; }

        // Signed area (positive = CCW winding in XY plane)
        float Area = 0.f;
        for (int32 i = 0; i < N; ++i)
        {
            const int32 j = (i + 1) % N;
            Area += Poly[i].X * Poly[j].Y - Poly[j].X * Poly[i].Y;
        }
        const bool bCCW = (Area > 0.f);

        // Working index list
        TArray<int32> Idx;
        Idx.Reserve(N);
        for (int32 i = 0; i < N; ++i) Idx.Add(i);

        // Point-in-triangle test (sign of cross products)
        auto InTriangle = [](const FVector& A, const FVector& B, const FVector& C,
                              const FVector& P) -> bool
        {
            const float d1 = (P.X-B.X)*(A.Y-B.Y) - (A.X-B.X)*(P.Y-B.Y);
            const float d2 = (P.X-C.X)*(B.Y-C.Y) - (B.X-C.X)*(P.Y-C.Y);
            const float d3 = (P.X-A.X)*(C.Y-A.Y) - (C.X-A.X)*(P.Y-A.Y);
            return !((d1<0||d2<0||d3<0) && (d1>0||d2>0||d3>0));
        };

        auto IsEar = [&](int32 pi, int32 ci, int32 ni) -> bool
        {
            const FVector& A = Poly[Idx[pi]];
            const FVector& B = Poly[Idx[ci]];
            const FVector& C = Poly[Idx[ni]];
            // Must be a convex vertex for current winding
            const float Cross = (B.X-A.X)*(C.Y-A.Y) - (B.Y-A.Y)*(C.X-A.X);
            if (bCCW ? (Cross <= 0.f) : (Cross >= 0.f)) return false;
            // No other polygon vertex may lie inside this triangle
            const int32 Rem = Idx.Num();
            for (int32 k = 0; k < Rem; ++k)
            {
                if (k==pi || k==ci || k==ni) continue;
                if (InTriangle(A, B, C, Poly[Idx[k]])) return false;
            }
            return true;
        };

        int32 Remaining = Idx.Num();
        int32 Curr = 0;
        int32 Attempts = 0;
        const int32 MaxAttempts = Remaining * Remaining + 1;
        while (Remaining > 3 && Attempts <= MaxAttempts)
        {
            const int32 Prev = (Curr - 1 + Remaining) % Remaining;
            const int32 Next = (Curr + 1) % Remaining;
            if (IsEar(Prev, Curr, Next))
            {
                OutTris.Add(Idx[Prev]);
                OutTris.Add(Idx[Curr]);
                OutTris.Add(Idx[Next]);
                Idx.RemoveAt(Curr);
                --Remaining;
                if (Curr >= Remaining) Curr = 0;
                Attempts = 0;
            }
            else
            {
                Curr = (Curr + 1) % Remaining;
                ++Attempts;
            }
        }
        if (Remaining == 3)
        {
            OutTris.Add(Idx[0]);
            OutTris.Add(Idx[1]);
            OutTris.Add(Idx[2]);
        }
    };

    auto FlushBuilding = [&]()
    {
        // OSM polygons close themselves by repeating the first node as the last.
        // Remove that duplicate before triangulating — mirrors db_query.cpp line 174-177.
        if (Nodes.Num() > 1)
        {
            const FVector& First = Nodes[0];
            const FVector& Last  = Nodes.Last();
            if (FMath::IsNearlyEqual(First.X, Last.X, 1.f) &&
                FMath::IsNearlyEqual(First.Y, Last.Y, 1.f))
            {
                Nodes.Pop();
            }
        }

        const int32 N = Nodes.Num();
        if (N < 3) return;

        const FLinearColor Color  = BuildingColor(CurBldType);
        const float        H      = BuildingHeightCm(CurBldType);
        const uint32       Key    = ColorKey(Color);

        FBuildingSection& Sec = Groups.FindOrAdd(Key);
        Sec.Color = Color;

        // Z coordinates in local space: ground = -ActorZ (→ world 0), top = H-ActorZ (→ world H)
        const float ZGnd = -ActorZ;
        const float ZTop = H - ActorZ;

        // ── Wall quads (one per polygon edge) ────────────────────────────
        for (int32 i = 0; i < N; ++i)
        {
            const int32   j   = (i + 1) % N;
            const FVector v0(Nodes[i].X, Nodes[i].Y, ZGnd);
            const FVector v1(Nodes[j].X, Nodes[j].Y, ZGnd);
            const FVector v2(Nodes[j].X, Nodes[j].Y, ZTop);
            const FVector v3(Nodes[i].X, Nodes[i].Y, ZTop);

            const FVector Edge = (v1 - v0).GetSafeNormal();
            const FVector WallNormal(-Edge.Y, Edge.X, 0.f);

            const int32 Base = Sec.Vertices.Num();
            Sec.Vertices.Add(v0);   Sec.Normals.Add( WallNormal);
            Sec.Vertices.Add(v1);   Sec.Normals.Add( WallNormal);
            Sec.Vertices.Add(v2);   Sec.Normals.Add( WallNormal);
            Sec.Vertices.Add(v3);   Sec.Normals.Add( WallNormal);
            Sec.Triangles.Add(Base+0); Sec.Triangles.Add(Base+1); Sec.Triangles.Add(Base+2);
            Sec.Triangles.Add(Base+0); Sec.Triangles.Add(Base+2); Sec.Triangles.Add(Base+3);

            // Back-face (reversed winding) — two-sided walls without a two-sided material
            const int32 Back = Sec.Vertices.Num();
            Sec.Vertices.Add(v0);   Sec.Normals.Add(-WallNormal);
            Sec.Vertices.Add(v2);   Sec.Normals.Add(-WallNormal);
            Sec.Vertices.Add(v1);   Sec.Normals.Add(-WallNormal);
            Sec.Vertices.Add(v3);   Sec.Normals.Add(-WallNormal);
            Sec.Triangles.Add(Back+0); Sec.Triangles.Add(Back+1); Sec.Triangles.Add(Back+2);
            Sec.Triangles.Add(Back+0); Sec.Triangles.Add(Back+3); Sec.Triangles.Add(Back+1);
        }

        // ── Roof — ear-clipping triangulation for correct concave polygons ──
        // Building roofs at height H, normal pointing up (+Z).
        TArray<int32> RoofTris;
        Triangulate2D(Nodes, RoofTris);

        const FVector RoofNormal(0.f, 0.f, 1.f);
        const int32 RoofBase = Sec.Vertices.Num();

        // Add all roof vertices at world height H (local = ZTop)
        for (const FVector& N2 : Nodes)
            Sec.Vertices.Add(FVector(N2.X, N2.Y, ZTop)), Sec.Normals.Add(RoofNormal);

        // Map ear-clipping indices (0..N-1) to section-global indices
        for (int32 Idx : RoofTris)
            Sec.Triangles.Add(RoofBase + Idx);

        // Back-face (reversed winding) - same approach as the wall quads above.
        // Triangulate2D emits triangles in the polygon's own winding order, and
        // OSM footprints arrive in both windings, so a single-sided roof faces
        // downward for roughly half of all buildings and is invisible from the
        // air. Emitting both orientations makes the roof visible regardless.
        const int32 RoofBackBase = Sec.Vertices.Num();
        for (const FVector& N2 : Nodes)
            Sec.Vertices.Add(FVector(N2.X, N2.Y, ZTop)), Sec.Normals.Add(-RoofNormal);

        for (int32 k = 0; k + 2 < RoofTris.Num(); k += 3)
        {
            Sec.Triangles.Add(RoofBackBase + RoofTris[k + 2]);
            Sec.Triangles.Add(RoofBackBase + RoofTris[k + 1]);
            Sec.Triangles.Add(RoofBackBase + RoofTris[k + 0]);
        }
    };

    for (const FBuildingRow& Row : CachedBuildings)
    {
        if (Row.BuildingId != CurBldId)
        {
            FlushBuilding();
            Nodes.Reset();
            CurBldId   = Row.BuildingId;
            CurBldType = Row.BuildingType;
        }
        // UE5: X = North (Lat), Y = East (Lon)
        const float wx = (float)((Row.Lat - RefLat) * MetresPerDegLat * 100.0);
        const float wy = (float)((Row.Lon - RefLon) * MetresPerDegLon * 100.0);
        Nodes.Add(FVector(wx, wy, 0.f));
    }
    FlushBuilding();

    // ── Upload each colour group as one ProceduralMesh section ─────────────
    int32 SectionIdx = 0;
    static const TArray<FVector2D>       EmptyUVs;
    static const TArray<FProcMeshTangent> EmptyTangents;

    for (auto& Pair : Groups)
    {
        FBuildingSection& Sec = Pair.Value;

        BuildingMesh->CreateMeshSection(
            SectionIdx,
            Sec.Vertices,
            Sec.Triangles,
            Sec.Normals,
            EmptyUVs,
            TArray<FColor>(),
            EmptyTangents,
            false);   // no collision — map viewer doesn't need physics

        // Tint this section with a material instance coloured to the group.
        if (BuildingBaseMaterial)
        {
            UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(
                BuildingBaseMaterial, this);
            Mat->SetVectorParameterValue(TEXT("Color"), Sec.Color);
            BuildingMesh->SetMaterial(SectionIdx, Mat);
        }

        ++SectionIdx;
    }
}

// ---------------------------------------------------------------------------
// ClearBuildingMesh — hide buildings when camera rises above threshold
// ---------------------------------------------------------------------------
void AMapViewActor::ClearBuildingMesh()
{
    BuildingMesh->ClearAllMeshSections();
}

// ---------------------------------------------------------------------------
// RedrawAll — ground + roads + buildings as meshes; POIs via DrawDebugLine.
//
// Roads are now ribbons in RoadMesh; only POIs still use DrawDebugLine.
// Buildings are handled by RebuildBuildingMesh() which writes into
// the persistent UProceduralMeshComponent — no flush needed for the mesh.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// RebuildGroundPlane - one large flat quad under everything.
//
// Without this the map floats on the renderer's clear colour, which reads as
// a black void and blows out the white building faces by contrast.
// ---------------------------------------------------------------------------
void AMapViewActor::RebuildGroundPlane()
{
    if (bGroundBuilt && bShowGround) return;   // static quad, build once
    GroundMesh->ClearAllMeshSections();
    bGroundBuilt = false;
    if (!bShowGround) return;

    const float Z = -GetActorLocation().Z;   // local z such that world z = 0
    const float E = GroundExtentCm;

    TArray<FVector> V {
        FVector(-E, -E, Z), FVector( E, -E, Z),
        FVector( E,  E, Z), FVector(-E,  E, Z) };
    TArray<FVector> N; N.Init(FVector(0.f, 0.f, 1.f), 4);
    TArray<int32>   T { 0,1,2, 0,2,3, 0,2,1, 0,3,2 };   // both windings, as the walls do

    GroundMesh->CreateMeshSection(0, V, T, N, TArray<FVector2D>(),
                                  TArray<FColor>(), TArray<FProcMeshTangent>(), false);

    if (BuildingBaseMaterial)
    {
        UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(BuildingBaseMaterial, this);
        Mat->SetVectorParameterValue(TEXT("Color"), GroundColor);
        GroundMesh->SetMaterial(0, Mat);
    }
    bGroundBuilt = true;
}

// ---------------------------------------------------------------------------
// RebuildRoadMesh - roads as width-extruded ribbons instead of debug lines.
//
// Each segment becomes a quad of RoadWidthCm(type) width, lying flat at
// RoadZOffsetCm above the ground. Segments are batched by colour into one
// mesh section per colour, the same pattern RebuildBuildingMesh uses, so the
// whole road network costs one draw call per road colour.
//
// Both windings are emitted, matching the two-sided building walls, so the
// ribbons are visible from above and below without a two-sided material.
// ---------------------------------------------------------------------------
void AMapViewActor::RebuildRoadMesh(float AltitudeM)
{
    RoadMesh->ClearAllMeshSections();
    if (CachedRows.Num() == 0) { DrawnSegmentCount = 0; TotalSegmentCount = 0; return; }

    const float Z  = RoadZOffsetCm - GetActorLocation().Z;
    const FVector Up(0.f, 0.f, 1.f);

    // Key by (class, colour) so a LOD change can hide whole sections.
    TMap<uint64, FBuildingSection> Groups;
    TMap<uint64, int32>            GroupClass;
    auto GroupKey = [](int32 Cls, const FLinearColor& C) -> uint64 {
        return (uint64(Cls) << 32) |
               (uint32(C.R * 255) << 16) | (uint32(C.G * 255) << 8) | uint32(C.B * 255);
    };

    int32   Drawn = 0, Total = 0;
    int32   PrevRoadId = -1;
    double  PrevLat = 0.0, PrevLon = 0.0;
    FString PrevType;
    bool    PrevVis = false;

    for (const FRoadRow& Row : CachedRows)
    {
        // Geometry for every class is built once; ApplyRoadLod decides what shows.
        const bool bVis = true;
        if (Row.RoadId == PrevRoadId)
        {
            ++Total;
            if (PrevVis && bVis)
            {
                // UE5: X = North (Lat), Y = East (Lon)
                const float px = (float)((PrevLat - RefLat) * MetresPerDegLat * 100.0);
                const float py = (float)((PrevLon - RefLon) * MetresPerDegLon * 100.0);
                const float wx = (float)((Row.Lat  - RefLat) * MetresPerDegLat * 100.0);
                const float wy = (float)((Row.Lon  - RefLon) * MetresPerDegLon * 100.0);

                FVector Dir(wx - px, wy - py, 0.f);
                if (!Dir.IsNearlyZero())
                {
                    Dir = Dir.GetSafeNormal();
                    const FVector Side(-Dir.Y, Dir.X, 0.f);
                    const float   Half = RoadWidthCm(PrevType) * 0.5f;

                    const FColor C8 = RoadTypeColor(PrevType);
                    const FLinearColor C(C8.R / 255.f, C8.G / 255.f, C8.B / 255.f);

                    const int32  Cls = RoadClassOf(PrevType);
                    const uint64 Key = GroupKey(Cls, C);
                    FBuildingSection& Sec = Groups.FindOrAdd(Key);
                    Sec.Color = C;
                    GroupClass.FindOrAdd(Key) = Cls;

                    const FVector A(px, py, Z), B(wx, wy, Z);
                    const int32 Base = Sec.Vertices.Num();
                    Sec.Vertices.Add(A + Side * Half); Sec.Normals.Add(Up);
                    Sec.Vertices.Add(B + Side * Half); Sec.Normals.Add(Up);
                    Sec.Vertices.Add(B - Side * Half); Sec.Normals.Add(Up);
                    Sec.Vertices.Add(A - Side * Half); Sec.Normals.Add(Up);

                    Sec.Triangles.Add(Base+0); Sec.Triangles.Add(Base+1); Sec.Triangles.Add(Base+2);
                    Sec.Triangles.Add(Base+0); Sec.Triangles.Add(Base+2); Sec.Triangles.Add(Base+3);
                    Sec.Triangles.Add(Base+0); Sec.Triangles.Add(Base+2); Sec.Triangles.Add(Base+1);
                    Sec.Triangles.Add(Base+0); Sec.Triangles.Add(Base+3); Sec.Triangles.Add(Base+2);
                    ++Drawn;
                }
            }
        }
        PrevRoadId = Row.RoadId;
        PrevLat    = Row.Lat;
        PrevLon    = Row.Lon;
        PrevType   = Row.HighwayType;
        PrevVis    = bVis;
    }

    DrawnSegmentCount = Drawn;
    TotalSegmentCount = Total;

    UE_LOG(LogTemp, Log,
        TEXT("RebuildRoadMesh: rows=%d drawn=%d total=%d colourGroups=%d alt=%.0fm mat=%s"),
        CachedRows.Num(), Drawn, Total, Groups.Num(), (double)AltitudeM,
        BuildingBaseMaterial ? TEXT("ok") : TEXT("NULL"));

    RoadSectionClass.Reset();
    int32 SectionIdx = 0;
    for (auto& Pair : Groups)
    {
        FBuildingSection& Sec = Pair.Value;
        RoadSectionClass.Add(GroupClass[Pair.Key]);
        RoadMesh->CreateMeshSection(SectionIdx, Sec.Vertices, Sec.Triangles, Sec.Normals,
                                    TArray<FVector2D>(), TArray<FColor>(),
                                    TArray<FProcMeshTangent>(), false);
        if (BuildingBaseMaterial)
        {
            UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(BuildingBaseMaterial, this);
            Mat->SetVectorParameterValue(TEXT("Color"), Sec.Color);
            RoadMesh->SetMaterial(SectionIdx, Mat);
        }
        ++SectionIdx;
    }

    ApplyRoadLod(AltitudeM);

    // Diagnostic overlay - the same segments as thick debug lines, 3 m up.
    // Lines but no ribbons => the mesh component is the problem, not the data.
    if (bDebugRoadLines)
    {
        int32   Lines  = 0;
        int32   PrevId = -1;
        double  PLat = 0.0, PLon = 0.0;
        FString PType;
        for (const FRoadRow& Row : CachedRows)
        {
            if (Row.RoadId == PrevId)
            {
                const float px = (float)((PLat - RefLat) * MetresPerDegLat * 100.0);
                const float py = (float)((PLon - RefLon) * MetresPerDegLon * 100.0);
                const float wx = (float)((Row.Lat - RefLat) * MetresPerDegLat * 100.0);
                const float wy = (float)((Row.Lon - RefLon) * MetresPerDegLon * 100.0);
                DrawDebugLine(GetWorld(), FVector(px, py, 300.f), FVector(wx, wy, 300.f),
                              RoadTypeColor(PType), true, -1.f, 0, 300.f);
                ++Lines;
            }
            PrevId = Row.RoadId; PLat = Row.Lat; PLon = Row.Lon; PType = Row.HighwayType;
        }
        UE_LOG(LogTemp, Warning,
               TEXT("RoadDiag: debugLines=%d meshSections=%d rows=%d roadMeshRegistered=%s"),
               Lines, RoadSectionClass.Num(), CachedRows.Num(),
               (RoadMesh && RoadMesh->IsRegistered()) ? TEXT("yes") : TEXT("NO"));
    }

}

void AMapViewActor::RedrawAll(float AltitudeM, bool bDataChanged)
{
    // -- 1. Ground + roads (ProceduralMesh ribbons) -------------------------
    FlushPersistentDebugLines(GetWorld());

    RebuildGroundPlane();

    if (bDataChanged)
        RebuildRoadMesh(AltitudeM);
    else
        ApplyRoadLod(AltitudeM);

    // -- 2. Buildings. Buildings have no LOD classes, so a pure LOD change
    //    must not re-triangulate them.
    if (!bDataChanged) return;

    if (AltitudeM <= ShowBuildingsAltM)
        RebuildBuildingMesh();
    else
        ClearBuildingMesh();

    // ── 3. POIs (DrawDebugLine, same as Phase 6) ──────────────────────────
    if (AltitudeM <= ShowPoisAltM && CachedPOIs.Num() > 0)
    {
        const FColor PoiColor(0, 220, 220);
        for (const FPoiRow& Row : CachedPOIs)
        {
            // UE5: X = North (Lat), Y = East (Lon)
            const float wx = (float)((Row.Lat - RefLat) * MetresPerDegLat * 100.0);
            const float wy = (float)((Row.Lon - RefLon) * MetresPerDegLon * 100.0);
            DrawDebugLine(GetWorld(),
                FVector(wx, wy, 0.f), FVector(wx, wy, 800.f),
                PoiColor, true, -1.f, 0, LineThickness);
        }
    }
}
