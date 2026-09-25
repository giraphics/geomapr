#include "MapViewerGameMode.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameFramework/PlayerController.h"

AMapViewerGameMode::AMapViewerGameMode()
{
    // Use spectator pawn — no collision, free flight, ideal for a map viewer.
    DefaultPawnClass = ASpectatorPawn::StaticClass();
}

void AMapViewerGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    // Pawn is fully possessed here — safe to position the camera.
    if (NewPlayer && NewPlayer->GetPawn())
    {
        // 500 m above origin, looking straight down.
        NewPlayer->GetPawn()->SetActorLocation(FVector(0.f, 0.f, 50000.f));
        NewPlayer->SetControlRotation(FRotator(-89.f, 0.f, 0.f));
    }
}
