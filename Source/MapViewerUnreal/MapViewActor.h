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
 * Roads and POI poles continue to use DrawDebugLine (unchanged from Phase 6).
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
	void RedrawAll(float AltitudeM);

	// Rebuild the ProceduralMeshComponent from CachedBuildings.
	// Called from RedrawAll when buildings are visible.
	// Groups building geometry by colour — one mesh section per colour group.
	void RebuildBuildingMesh();

	// Clear all ProceduralMesh sections (called when altitude > ShowBuildingsAltM).
	void ClearBuildingMesh();

	// Road LOD helpers
	static FColor RoadTypeColor(const FString& Type);
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
