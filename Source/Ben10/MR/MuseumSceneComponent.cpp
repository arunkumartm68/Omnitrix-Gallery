// Alien Museum - passthrough, scene permission, MRUK room loading, surface raycasts, occluders.

#include "MR/MuseumSceneComponent.h"
#include "Core/MuseumAssets.h"
#include "Ben10.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "MRUtilityKit.h"
#include "MRUtilityKitAnchor.h"
#include "MRUtilityKitRoom.h"
#include "MRUtilityKitSubsystem.h"
#include "OculusXRPassthroughSubsystem.h"
#include "OculusXRPersistentPassthroughInstance.h"
#include "AndroidPermissionFunctionLibrary.h"
#include "AndroidPermissionCallbackProxy.h"

namespace
{
	const TCHAR* ScenePermission = TEXT("com.oculus.permission.USE_SCENE");
}

UMuseumSceneComponent::UMuseumSceneComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	OccluderLabels = {
		FMRUKLabels::WallFace,
		FMRUKLabels::Table,
		FMRUKLabels::Couch,
		FMRUKLabels::Storage,
		FMRUKLabels::Bed,
		FMRUKLabels::Screen,
		FMRUKLabels::Lamp,
		FMRUKLabels::Plant,
		FMRUKLabels::Other,
	};

	PlaceableLabels = {
		FMRUKLabels::Floor,
		FMRUKLabels::Table,
		FMRUKLabels::Storage,
		FMRUKLabels::Bed,
		FMRUKLabels::Couch,
		FMRUKLabels::Other,
	};
}

UMRUKSubsystem* UMuseumSceneComponent::GetMRUK() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UMRUKSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------------------------
// Start-up flow: passthrough -> permission -> load scene (-> capture) -> ready
// ---------------------------------------------------------------------------------------------

void UMuseumSceneComponent::StartScene()
{
	if (SceneState != EMuseumSceneState::NotStarted)
	{
		return;
	}

	StartPassthrough();

	if (!UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
	{
		UE_LOG(LogAlienMuseum, Log, TEXT("No headset active: using the editor room fallback (floor at Z=%.0f)"), FallbackFloorZ);
		FinishScene(false);
		return;
	}

	RequestPermissionThenLoad();
}

void UMuseumSceneComponent::StartPassthrough()
{
	if (!bEnablePassthrough || !UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
	{
		return;
	}

	if (UOculusXRPassthroughSubsystem* Passthrough = UOculusXRPassthroughSubsystem::GetPassthroughSubsystem(GetWorld()))
	{
		// Default parameters: visible reconstructed passthrough rendered as an underlay.
		FOculusXRPersistentPassthroughParameters Parameters;
		Passthrough->InitializePersistentPassthrough(Parameters, FOculusXRPassthrough_LayerResumed_Single());
		UE_LOG(LogAlienMuseum, Log, TEXT("Passthrough started"));
	}
	else
	{
		UE_LOG(LogAlienMuseum, Warning, TEXT("Passthrough subsystem unavailable. Is 'Passthrough Enabled' ticked in Project Settings > Meta XR?"));
	}
}

void UMuseumSceneComponent::RequestPermissionThenLoad()
{
#if PLATFORM_ANDROID
	if (!UAndroidPermissionFunctionLibrary::CheckPermission(ScenePermission))
	{
		SceneState = EMuseumSceneState::RequestingPermission;
		if (UAndroidPermissionCallbackProxy* Proxy = UAndroidPermissionFunctionLibrary::AcquirePermissions({ ScenePermission }))
		{
			Proxy->OnPermissionsGrantedDynamicDelegate.AddUniqueDynamic(this, &UMuseumSceneComponent::HandlePermissionsResult);
			UE_LOG(LogAlienMuseum, Log, TEXT("Requesting spatial data permission"));
			return;
		}
	}
#endif
	LoadSceneFromDevice();
}

void UMuseumSceneComponent::HandlePermissionsResult(const TArray<FString>& Permissions, const TArray<bool>& GrantResults)
{
	if (SceneState != EMuseumSceneState::RequestingPermission)
	{
		return;
	}
	bool bGranted = false;
	for (int32 i = 0; i < Permissions.Num(); ++i)
	{
		if (Permissions[i] == ScenePermission && GrantResults.IsValidIndex(i) && GrantResults[i])
		{
			bGranted = true;
		}
	}
	if (bGranted)
	{
		LoadSceneFromDevice();
	}
	else
	{
		UE_LOG(LogAlienMuseum, Warning, TEXT("Spatial data permission denied: chambers can only be placed on the fallback floor"));
		FinishScene(false);
	}
}

void UMuseumSceneComponent::LoadSceneFromDevice()
{
	UMRUKSubsystem* MRUK = GetMRUK();
	if (!MRUK)
	{
		UE_LOG(LogAlienMuseum, Warning, TEXT("MR Utility Kit subsystem not found"));
		FinishScene(false);
		return;
	}
	SceneState = EMuseumSceneState::LoadingScene;
	MRUK->OnSceneLoaded.AddUniqueDynamic(this, &UMuseumSceneComponent::HandleSceneLoaded);
	UE_LOG(LogAlienMuseum, Log, TEXT("Loading room from device"));
	MRUK->LoadSceneFromDevice();
}

void UMuseumSceneComponent::HandleSceneLoaded(bool bSuccess)
{
	UMRUKSubsystem* MRUK = GetMRUK();

	// After start-up this event also fires when the room model changes: refresh the occluders.
	if (SceneState == EMuseumSceneState::Ready)
	{
		if (bSuccess && bUsingDeviceScene && bEnableSceneOccluders)
		{
			ClearOccluders();
			BuildOccluders();
		}
		return;
	}
	if (SceneState != EMuseumSceneState::LoadingScene)
	{
		return;
	}

	if (bSuccess && MRUK && MRUK->GetCurrentRoom())
	{
		UE_LOG(LogAlienMuseum, Log, TEXT("Room loaded: %d room(s)"), MRUK->Rooms.Num());
		FinishScene(true);
		return;
	}

	if (MRUK && bLaunchSceneCaptureIfMissing && !bCaptureAttempted)
	{
		bCaptureAttempted = true;
		SceneState = EMuseumSceneState::CapturingScene;
		MRUK->OnCaptureComplete.AddUniqueDynamic(this, &UMuseumSceneComponent::HandleCaptureComplete);
		UE_LOG(LogAlienMuseum, Log, TEXT("No room model found, launching Space Setup"));
		if (MRUK->LaunchSceneCapture())
		{
			return;
		}
	}

	UE_LOG(LogAlienMuseum, Warning, TEXT("Room could not be loaded, using the fallback floor"));
	FinishScene(false);
}

void UMuseumSceneComponent::HandleCaptureComplete(bool bSuccess)
{
	if (SceneState != EMuseumSceneState::CapturingScene)
	{
		return;
	}
	if (bSuccess)
	{
		LoadSceneFromDevice();
	}
	else
	{
		FinishScene(false);
	}
}

void UMuseumSceneComponent::FinishScene(bool bDeviceScene)
{
	bUsingDeviceScene = bDeviceScene;
	SceneState = EMuseumSceneState::Ready;
	if (bDeviceScene && bEnableSceneOccluders)
	{
		BuildOccluders();
	}
	OnSceneReady.Broadcast(bDeviceScene);
}

void UMuseumSceneComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMRUKSubsystem* MRUK = GetMRUK())
	{
		MRUK->OnSceneLoaded.RemoveDynamic(this, &UMuseumSceneComponent::HandleSceneLoaded);
		MRUK->OnCaptureComplete.RemoveDynamic(this, &UMuseumSceneComponent::HandleCaptureComplete);
	}
	ClearOccluders();
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------
// Surface queries
// ---------------------------------------------------------------------------------------------

EMuseumSurfaceType UMuseumSceneComponent::ClassifyLabels(const TArray<FString>& Labels) const
{
	if (Labels.Contains(FMRUKLabels::Floor))
	{
		return EMuseumSurfaceType::Floor;
	}
	for (const FString& Label : Labels)
	{
		if (PlaceableLabels.Contains(Label))
		{
			return EMuseumSurfaceType::Table;
		}
	}
	return EMuseumSurfaceType::Other;
}

FMuseumSurfaceHit UMuseumSceneComponent::RaycastSurface(const FVector& Origin, const FVector& Direction, float MaxDistance) const
{
	FMuseumSurfaceHit Result;
	const FVector Dir = Direction.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return Result;
	}

	// --- Real room (MR Utility Kit) ---
	UMRUKSubsystem* MRUK = bUsingDeviceScene ? GetMRUK() : nullptr;
	if (MRUK && MRUK->GetCurrentRoom())
	{
		FMRUKLabelFilter Filter;
		Filter.IncludedLabels = PlaceableLabels;
		FMRUKHit Hit;
		if (AMRUKAnchor* Anchor = MRUK->Raycast(Origin, Dir, MaxDistance, Filter, Hit))
		{
			Result.bHit = true;
			Result.Location = Hit.HitPosition;
			Result.Normal = Hit.HitNormal;
			Result.Surface = ClassifyLabels(Anchor->SemanticClassifications);
			Result.Label = Anchor->SemanticClassifications.Num() > 0 ? Anchor->SemanticClassifications[0] : FString();
		}
		return Result;
	}

	// --- Editor / no-headset fallback: level geometry, then an infinite floor ---
	if (const UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MuseumSurface), false, GetOwner());
		// Only static level geometry: chambers, aliens and UI are WorldDynamic / Pawn.
		const FCollisionObjectQueryParams ObjectParams(ECC_WorldStatic);
		if (World->LineTraceSingleByObjectType(Hit, Origin, Origin + Dir * MaxDistance, ObjectParams, Params))
		{
			Result.bHit = true;
			Result.Location = Hit.ImpactPoint;
			Result.Normal = Hit.ImpactNormal;
			Result.Label = TEXT("EDITOR");
			Result.Surface = Hit.ImpactNormal.Z < 0.7f ? EMuseumSurfaceType::Other
				: (Hit.ImpactPoint.Z - FallbackFloorZ < 5.f ? EMuseumSurfaceType::Floor : EMuseumSurfaceType::Table);
			return Result;
		}
	}

	if (Dir.Z < -0.01f)
	{
		const float T = (FallbackFloorZ - Origin.Z) / Dir.Z;
		if (T > 0.f && T <= MaxDistance)
		{
			Result.bHit = true;
			Result.Location = Origin + Dir * T;
			Result.Normal = FVector::UpVector;
			Result.Surface = EMuseumSurfaceType::Floor;
			Result.Label = TEXT("FALLBACK_FLOOR");
		}
	}
	return Result;
}

FMuseumSurfaceHit UMuseumSceneComponent::FindSurfaceBelow(const FVector& Location, float MaxDrop) const
{
	// Start a little above the chamber bottom so a chamber pushed slightly into a table lands on top.
	const float StartLift = 15.f;
	return RaycastSurface(Location + FVector(0.f, 0.f, StartLift), FVector::DownVector, MaxDrop + StartLift);
}

float UMuseumSceneComponent::GetFloorZ() const
{
	if (UMRUKSubsystem* MRUK = bUsingDeviceScene ? GetMRUK() : nullptr)
	{
		if (AMRUKRoom* Room = MRUK->GetCurrentRoom())
		{
			if (Room->FloorAnchors.Num() > 0 && Room->FloorAnchors[0])
			{
				return Room->FloorAnchors[0]->GetActorLocation().Z;
			}
		}
	}
	return FallbackFloorZ;
}

// ---------------------------------------------------------------------------------------------
// Real-world occlusion from the room model
// ---------------------------------------------------------------------------------------------

void UMuseumSceneComponent::BuildOccluders()
{
	UMRUKSubsystem* MRUK = GetMRUK();
	if (!MRUK)
	{
		return;
	}

	UMaterialInterface* Material = bShowRoomDebug
		? MuseumAssets::HologramMaterial()
		: (OccluderMaterialOverride ? OccluderMaterialOverride.Get() : MuseumAssets::OccluderMaterial());

	const TArray<FString> CutHoles = { FMRUKLabels::DoorFrame, FMRUKLabels::WindowFrame, FMRUKLabels::Opening };
	const TArray<FMRUKPlaneUV> NoUVAdjustments;

	for (AMRUKRoom* Room : MRUK->Rooms)
	{
		if (!Room)
		{
			continue;
		}
		for (AMRUKAnchor* Anchor : Room->AllAnchors)
		{
			if (!Anchor || !Anchor->GetRootComponent() || !Anchor->HasAnyLabel(OccluderLabels))
			{
				continue;
			}

			UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Anchor, MakeUniqueObjectName(Anchor, UProceduralMeshComponent::StaticClass(), TEXT("MuseumOccluder")));
			Mesh->SetupAttachment(Anchor->GetRootComponent());
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCastShadow(false);
			Mesh->SetCanEverAffectNavigation(false);
			Mesh->RegisterComponent();

			const bool bPreferVolume = Anchor->VolumeBounds.IsValid != 0;
			Anchor->GenerateProceduralAnchorMesh(Mesh, NoUVAdjustments, CutHoles, bPreferVolume, false, OccluderInflate);

			Mesh->SetMaterial(0, Material);
			// Draw before the glass/holograms so translucent museum parts are not erased.
			Mesh->SetTranslucentSortPriority(-1);
			Occluders.Add(Mesh);
		}
	}
	UE_LOG(LogAlienMuseum, Log, TEXT("Built %d real-world occluders%s"), Occluders.Num(), bShowRoomDebug ? TEXT(" (debug view)") : TEXT(""));
}

void UMuseumSceneComponent::ClearOccluders()
{
	for (UProceduralMeshComponent* Mesh : Occluders)
	{
		if (IsValid(Mesh))
		{
			Mesh->DestroyComponent();
		}
	}
	Occluders.Reset();
}

void UMuseumSceneComponent::SetShowRoomDebug(bool bShow)
{
	bShowRoomDebug = bShow;
	if (SceneState == EMuseumSceneState::Ready && bUsingDeviceScene)
	{
		ClearOccluders();
		BuildOccluders();
	}
}
