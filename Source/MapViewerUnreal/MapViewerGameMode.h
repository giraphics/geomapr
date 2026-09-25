#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MapViewerGameMode.generated.h"

UCLASS()
class MAPVIEWERUNREAL_API AMapViewerGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AMapViewerGameMode();

    // Called after a player fully logs in and possesses their pawn — safe to move camera here.
    virtual void PostLogin(APlayerController* NewPlayer) override;
};
