#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "MapViewActor.generated.h"

// ---------------------------------------------------------------------------
// Road row — cached from the last spatial query so LOD-only changes can
// redraw from memory without a second DB hit.
// ---------------------------------------------------------------------------
struct FRoadRow
{
	int32   RoadId      = 0;
	FString HighwayType;
	double  Lat         = 0.0;
	double  Lon         = 0.0;
};

// ---------------------------------------------------------------------------
// Building row — one vertex of a building polygon, with the type for height
// and colour lookup.  Rows are grouped by BuildingId, in sequence order.
// ---------------------------------------------------------------------------
struct FBuildingRow
{
	int32   BuildingId   = 0;
	FString BuildingType;
	double  Lat          = 0.0;
	double  Lon          = 0.0;
};

// ---------------------------------------------------------------------------
// POI row — one point-of-interest position and type.
// ---------------------------------------------------------------------------
struct FPoiRow
{
	double  Lat     = 0.0;
	double  Lon     = 0.0;
	FString PoiType;
};

// ---------------------------------------------------------------------------
// One colour group of building geometry — mirrors phase6 BuildingGroup.
// All buildings of the same colour are batched into one ProceduralMesh
// section so UE5 draws them in a single draw call per colour.
// ---------------------------------------------------------------------------
struct FBuildingSection
{
	TArray<FVector>    Vertices;
	TArray<int32>      Triangles;
	TArray<FVector>    Normals;
	FLinearColor       Color;
};

/**
 * Phase 6 Procedural Mesh — roads (LOD) + filled 3D buildings + POI poles.
 *
 * Extends Phase 6 by replacing DrawDebugLine building wireframes with a
 * UProceduralMeshComponent that renders solid filled triangles, exactly
 * mirroring phase6_3d_world_cpp's wall-quad + roof-fan geometry:
 *
 *   Wall quad (per polygon edge i → (i+1)%n):
 *     v0=(wxi,wyi,0)  v1=(wxj,wyj,0)  v2=(wxj,wyj,h)  v3=(wxi,wyi,h)
 *     Triangle A: {v0, v1, v2}   Triangle B: {v0, v2, v3}
 *
 *   Roof fan (from nodes[0] at height h):
 *     For k = 1 .. n-2: {nodes[0], nodes[k], nodes[k+1]}
 *
 * Buildings are batched by colour into one mesh section per colour group,
 * each section receiving its own UMaterialInstanceDynamic so the colour is
 * set at runtime without a custom material asset.
 *
 * Roads are width-extruded ribbons in RoadMesh, batched one section per
 * colour, over a flat GroundMesh quad. Only POI poles still use DrawDebugLine.
 */
UCLASS()
class MAPVIEWERUNREAL_API AMapViewActor : public AActor
{
	GENERATED_BODY()

public:
	AMapViewActor();

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// ── Inspector-settable properties ──────────────────────────────────────

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FString DatabasePath;

	// Point DatabasePath at a database that actually exists.  A path saved into
	// a level can outlive the folder it referred to (or belong to another
	// machine), and the only symptom is that nothing renders at all.  This
	// falls back to the copy the project ships in Data/map_extended.db.
	void EnsureDatabasePath();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	float LineThickness = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Cache")
	float CacheMargin = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Cache")
	float CheckIntervalSeconds = 0.1f;

	// LOD tier altitude thresholds (metres, camera above map).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|LOD")
	float Lod0AltM = 40000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|LOD")
	float Lod1AltM = 12000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|LOD")
	float Lod2AltM = 3000.f;

	// Radius (metres) used for the initial map query in editor mode.
	// Increase this to see a larger area when not in Play mode.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Editor")
	float EditorViewRadiusM = 3000.f;

	// Layer visibility thresholds — mirror phase6_3d_world_cpp constants.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Layers")
	float ShowBuildingsAltM = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Layers")
	float ShowPoisAltM = 600.f;

	// ── ProceduralMesh component for filled building geometry ──────────────

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Map")
	UProceduralMeshComponent* BuildingMesh = nullptr;

	// Roads as width-extruded ribbons (replaces DrawDebugLine)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Map")
	UProceduralMeshComponent* RoadMesh = nullptr;

	// Flat ground quad so the map does not sit on a black void
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Map")
	UProceduralMeshComponent* GroundMesh = nullptr;

	// -- Appearance ---------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Appearance")
	bool bShowGround = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Appearance")
	FLinearColor GroundColor = FLinearColor(0.28f, 0.29f, 0.30f);

	// Half-extent of the ground quad, cm. Default 10 km, which stays well
	// inside UE's ~21 km world bounds - a primitive whose bounds fall outside
	// the world is dropped by the renderer instead of drawn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Appearance")
	float GroundExtentCm = 1000000.f;

	// Roads sit this far above z=0 so they never z-fight with the ground
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Appearance")
	float RoadZOffsetCm = 4.f;

	// Diagnostic: also draw roads as debug lines on top of the ribbons.
	// If lines appear but ribbons do not, the mesh component is the problem,
	// not the data. Turn off once the ribbons are confirmed working.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map|Appearance")
	bool bDebugRoadLines = true;

	// ── Read-only stats ────────────────────────────────────────────────────

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Map|Stats")
	int32 QueryCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Map|Stats")
	int32 DrawnSegmentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Map|Stats")
	int32 TotalSegmentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Map|Stats")
	int32 BuildingCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Map|Stats")
	int32 PoiCount = 0;

private:
	// ── Internal helpers ───────────────────────────────────────────────────

	bool  InitRefOrigin();
	float GetCameraAltitudeM() const;
	void  GetCameraGeoBounds(double& OutMinLon, double& OutMaxLon,
	                         double& OutMinLat, double& OutMaxLat) const;

	// Road layer
	void QueryAndStore(double MinLon, double MaxLon, double MinLat, double MaxLat);

	// Building layer
	void QueryAndStoreBuildings(double MinLon, double MaxLon, double MinLat, double MaxLat);

	// POI layer
	void QueryAndStorePOIs(double MinLon, double MaxLon, double MinLat, double MaxLat);

	// Redraw roads + POIs via DrawDebugLine; rebuild BuildingMesh sections.
	// bDataChanged=false means only the LOD moved: roads just change section
	// visibility, nothing is rebuilt.
	void RedrawAll(float AltitudeM, bool bDataChanged = true);

	// Toggle road mesh sections by class instead of rebuilding geometry.
	void ApplyRoadLod(float AltM);

	// Rebuild roads as coloured ribbons from CachedRows.
	void RebuildRoadMesh(float AltitudeM);

	// Rebuild the flat ground quad.
	void RebuildGroundPlane();

	// Rebuild the ProceduralMeshComponent from CachedBuildings.
	// Called from RedrawAll when buildings are visible.
	// Groups building geometry by colour — one mesh section per colour group.
	void RebuildBuildingMesh();

	// Clear all ProceduralMesh sections (called when altitude > ShowBuildingsAltM).
	void ClearBuildingMesh();

	// Road LOD helpers
	static FColor RoadTypeColor(const FString& Type);
	// Ribbon width in cm by highway type.
	static float  RoadWidthCm(const FString& Type);
	// Road class 0-3; mirrors IsTypeVisible's tiers. Class <= Lod means visible.
	static int32  RoadClassOf(const FString& RawType);
	int32 LodLevel(float AltM) const;
	bool  IsTypeVisible(const FString& Type, float AltM) const;

	// Building helpers — mirror phase6 buildingColor() / buildingHeight()
	static FLinearColor BuildingColor(const FString& Type);
	// Levels=0 → use type-based approximation; Levels>0 → use real DB value.
	static float        BuildingHeightCm(const FString& Type, int32 Levels = 0);

	// ── Coordinate reference ───────────────────────────────────────────────
	double RefLat = 0.0, RefLon = 0.0;
	double MetresPerDegLon = 0.0, MetresPerDegLat = 0.0;
	bool   bRefInitialised = false;

	// ── Road dirty-flag cache ──────────────────────────────────────────────
	double CacheMinLon = 0.0, CacheMaxLon = 0.0;
	double CacheMinLat = 0.0, CacheMaxLat = 0.0;
	bool   bNeedsRefresh = true;
	float  TimeSinceLastCheck = 0.f;
	float  LastQueryAltitudeM = 0.f;
	float  LastDrawAltitudeM  = -1.f;
	TArray<FRoadRow> CachedRows;

	// Road class of each RoadMesh section, parallel to section index.
	TArray<int32> RoadSectionClass;

	// The ground quad is static; build it once.
	bool bGroundBuilt = false;

	// ── Building dirty-flag cache ──────────────────────────────────────────
	double BldCacheMinLon = 0.0, BldCacheMaxLon = 0.0;
	double BldCacheMinLat = 0.0, BldCacheMaxLat = 0.0;
	bool   bBldNeedsRefresh = true;
	TArray<FBuildingRow> CachedBuildings;

	// ── POI dirty-flag cache ───────────────────────────────────────────────
	double PoiCacheMinLon = 0.0, PoiCacheMaxLon = 0.0;
	double PoiCacheMinLat = 0.0, PoiCacheMaxLat = 0.0;
	bool   bPoiNeedsRefresh = true;
	TArray<FPoiRow> CachedPOIs;

	// Base material loaded once in the constructor; used to create per-section
	// UMaterialInstanceDynamic instances with the correct building colour.
	UMaterial* BuildingBaseMaterial = nullptr;
};
