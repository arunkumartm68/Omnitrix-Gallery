// Alien Museum - builds and animates the placeholder alien body from an AlienDataAsset.

#include "Aliens/AlienAppearanceComponent.h"
#include "Data/AlienDataAsset.h"
#include "Core/MuseumAssets.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"

namespace
{
	/** Basic shapes are 100 cm across, so a diameter in cm maps to scale Diameter/100. */
	FVector SizeToScale(float X, float Y, float Z)
	{
		return FVector(X, Y, Z) / 100.f;
	}
}

UAlienAppearanceComponent::UAlienAppearanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // the owning actor drives UpdateAnimation()
}

USceneComponent* UAlienAppearanceComponent::AddPivot(const TCHAR* BaseName, USceneComponent* Parent, const FTransform& Relative)
{
	AActor* Owner = GetOwner();
	USceneComponent* Pivot = NewObject<USceneComponent>(Owner, MakeUniqueObjectName(Owner, USceneComponent::StaticClass(), BaseName));
	Pivot->SetupAttachment(Parent);
	Pivot->SetRelativeTransform(Relative);
	Pivot->RegisterComponent();
	Parts.Add(Pivot);
	return Pivot;
}

UStaticMeshComponent* UAlienAppearanceComponent::AddPart(const TCHAR* BaseName, UStaticMesh* Mesh, USceneComponent* Parent, const FTransform& Relative, UMaterialInterface* Material)
{
	AActor* Owner = GetOwner();
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner, MakeUniqueObjectName(Owner, UStaticMeshComponent::StaticClass(), BaseName));
	Part->SetStaticMesh(Mesh);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	Part->SetCastShadow(bCastShadows);
	Part->SetupAttachment(Parent);
	Part->SetRelativeTransform(Relative);
	if (Material)
	{
		Part->SetMaterial(0, Material);
	}
	Part->RegisterComponent();
	Parts.Add(Part);
	return Part;
}

void UAlienAppearanceComponent::ClearAppearance()
{
	// Destroy children first (reverse creation order).
	for (int32 i = Parts.Num() - 1; i >= 0; --i)
	{
		if (Parts[i])
		{
			Parts[i]->DestroyComponent();
		}
	}
	Parts.Reset();
	AntennaPivots.Reset();
	Eyes.Reset();
	EyeBaseScales.Reset();
	AntennaBaseRoll.Reset();
	Materials.Reset();
	BodyPivot = nullptr;
	HeadPivot = nullptr;
	FootL = nullptr;
	FootR = nullptr;
}

void UAlienAppearanceComponent::BuildAppearance(const UAlienDataAsset* Data)
{
	ClearAppearance();
	if (!Data || !GetOwner())
	{
		return;
	}

	const float H = Data->Height;
	ModelHeight = H;
	Energy = Data->Energy;
	bHovers = Data->bHovers;
	HoverHeight = bHovers ? H * 0.10f : 0.f;

	UStaticMesh* Sphere = MuseumAssets::SphereMesh();
	UStaticMesh* Cylinder = MuseumAssets::CylinderMesh();

	// Materials (one dynamic instance per colour).
	UMaterialInterface* SkinBase = SkinMaterialOverride ? SkinMaterialOverride.Get() : MuseumAssets::AlienSkinMaterial();
	UMaterialInterface* GlowBase = GlowMaterialOverride ? GlowMaterialOverride.Get() : MuseumAssets::EmissiveMaterial();

	UMaterialInstanceDynamic* SkinMID = SkinBase ? UMaterialInstanceDynamic::Create(SkinBase, this) : nullptr;
	UMaterialInstanceDynamic* AccentMID = SkinBase ? UMaterialInstanceDynamic::Create(SkinBase, this) : nullptr;
	UMaterialInstanceDynamic* GlowMID = GlowBase ? UMaterialInstanceDynamic::Create(GlowBase, this) : nullptr;
	if (SkinMID)
	{
		SkinMID->SetVectorParameterValue(MuseumAssets::Params::Color, Data->SkinColor);
		SkinMID->SetVectorParameterValue(MuseumAssets::Params::RimColor, FMath::Lerp(Data->SkinColor, FLinearColor::White, 0.5f));
		Materials.Add(SkinMID);
	}
	if (AccentMID)
	{
		AccentMID->SetVectorParameterValue(MuseumAssets::Params::Color, Data->AccentColor);
		AccentMID->SetVectorParameterValue(MuseumAssets::Params::RimColor, Data->SkinColor);
		Materials.Add(AccentMID);
	}
	if (GlowMID)
	{
		GlowMID->SetVectorParameterValue(MuseumAssets::Params::Color, Data->EyeColor);
		GlowMID->SetScalarParameterValue(MuseumAssets::Params::Intensity, 1.6f);
		Materials.Add(GlowMID);
	}

	// ---- Body ----
	float HeadDiameter = 0.46f * H;
	float HeadOffset = 0.20f * H; // head centre above the neck pivot

	BodyPivot = AddPivot(TEXT("AlienBodyPivot"), this, FTransform(FVector(0.f, 0.f, HoverHeight)));

	switch (Data->BodyShape)
	{
	case EAlienBodyShape::Tall:
		AddPart(TEXT("AlienBody"), Cylinder, BodyPivot, FTransform(FQuat::Identity, FVector(0, 0, 0.30f * H), SizeToScale(0.30f * H, 0.30f * H, 0.52f * H)), SkinMID);
		HeadBaseZ = 0.56f * H;
		HeadDiameter = 0.40f * H;
		HeadOffset = 0.19f * H;
		ModelRadius = 0.20f * H;
		break;

	case EAlienBodyShape::Squat:
		AddPart(TEXT("AlienBody"), Sphere, BodyPivot, FTransform(FQuat::Identity, FVector(0, 0, 0.26f * H), SizeToScale(0.78f * H, 0.78f * H, 0.50f * H)), SkinMID);
		HeadBaseZ = 0.44f * H;
		HeadDiameter = 0.40f * H;
		HeadOffset = 0.17f * H;
		ModelRadius = 0.39f * H;
		break;

	case EAlienBodyShape::Blob:
	default:
		AddPart(TEXT("AlienBody"), Sphere, BodyPivot, FTransform(FQuat::Identity, FVector(0, 0, 0.30f * H), SizeToScale(0.60f * H, 0.60f * H, 0.58f * H)), SkinMID);
		HeadBaseZ = 0.55f * H;
		ModelRadius = 0.30f * H;
		break;
	}

	// Belly patch in the accent colour.
	AddPart(TEXT("AlienBelly"), Sphere, BodyPivot, FTransform(FQuat::Identity, FVector(ModelRadius * 0.62f, 0, 0.26f * H), SizeToScale(0.10f * H, 0.28f * H, 0.26f * H)), AccentMID);

	// ---- Head (not parented to the body pivot so squash does not distort it) ----
	HeadPivot = AddPivot(TEXT("AlienHeadPivot"), this, FTransform(FVector(0.f, 0.f, HeadBaseZ + HoverHeight)));
	AddPart(TEXT("AlienHead"), Sphere, HeadPivot, FTransform(FQuat::Identity, FVector(0, 0, HeadOffset), SizeToScale(HeadDiameter, HeadDiameter, HeadDiameter * 0.95f)), SkinMID);

	const float HeadR = HeadDiameter * 0.5f;

	// ---- Eyes ----
	const int32 EyeCount = FMath::Clamp(Data->EyeCount, 1, 3);
	const float EyeD = (EyeCount == 1 ? 0.16f : 0.10f) * H;
	TArray<FVector> EyePositions;
	if (EyeCount == 1)
	{
		EyePositions.Add(FVector(HeadR * 0.80f, 0.f, HeadOffset + HeadR * 0.10f));
	}
	else if (EyeCount == 2)
	{
		EyePositions.Add(FVector(HeadR * 0.78f, -HeadR * 0.38f, HeadOffset + HeadR * 0.12f));
		EyePositions.Add(FVector(HeadR * 0.78f, HeadR * 0.38f, HeadOffset + HeadR * 0.12f));
	}
	else
	{
		EyePositions.Add(FVector(HeadR * 0.76f, -HeadR * 0.46f, HeadOffset + HeadR * 0.05f));
		EyePositions.Add(FVector(HeadR * 0.84f, 0.f, HeadOffset + HeadR * 0.30f));
		EyePositions.Add(FVector(HeadR * 0.76f, HeadR * 0.46f, HeadOffset + HeadR * 0.05f));
	}
	for (const FVector& EyePos : EyePositions)
	{
		const FVector EyeScale = SizeToScale(EyeD * 0.7f, EyeD, EyeD);
		Eyes.Add(AddPart(TEXT("AlienEye"), Sphere, HeadPivot, FTransform(FQuat::Identity, EyePos, EyeScale), GlowMID));
		EyeBaseScales.Add(EyeScale);
	}

	// ---- Antennae ----
	if (Data->bHasAntennae)
	{
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const float BaseRoll = Side * 22.f;
			USceneComponent* Pivot = AddPivot(TEXT("AlienAntennaPivot"), HeadPivot,
				FTransform(FRotator(0.f, 0.f, BaseRoll), FVector(0.f, Side * HeadR * 0.35f, HeadOffset + HeadR * 0.80f)));
			AddPart(TEXT("AlienAntenna"), Cylinder, Pivot, FTransform(FQuat::Identity, FVector(0, 0, 0.11f * H), SizeToScale(0.03f * H, 0.03f * H, 0.22f * H)), AccentMID);
			AddPart(TEXT("AlienAntennaTip"), Sphere, Pivot, FTransform(FQuat::Identity, FVector(0, 0, 0.23f * H), SizeToScale(0.07f * H, 0.07f * H, 0.07f * H)), GlowMID);
			AntennaPivots.Add(Pivot);
			AntennaBaseRoll.Add(BaseRoll);
		}
	}

	// ---- Feet (walkers only) ----
	if (!bHovers)
	{
		const float FootD = 0.16f * H;
		FootLBase = FVector(0.05f * H, -0.14f * H, FootD * 0.28f);
		FootRBase = FVector(0.05f * H, 0.14f * H, FootD * 0.28f);
		FootL = AddPart(TEXT("AlienFootL"), Sphere, this, FTransform(FQuat::Identity, FootLBase, SizeToScale(FootD * 1.3f, FootD, FootD * 0.55f)), AccentMID);
		FootR = AddPart(TEXT("AlienFootR"), Sphere, this, FTransform(FQuat::Identity, FootRBase, SizeToScale(FootD * 1.3f, FootD, FootD * 0.55f)), AccentMID);
	}

	AnimTime = FMath::FRand() * 10.f;
	BlinkTimer = FMath::FRandRange(1.f, 4.f);
	HeadRotation = FRotator::ZeroRotator;
}

void UAlienAppearanceComponent::UpdateAnimation(float DeltaSeconds, float SpeedAlpha, const FVector* LookTarget, bool bExcited)
{
	if (!BodyPivot || !HeadPivot)
	{
		return;
	}

	const float Excite = bExcited ? 1.f : 0.f;
	SpeedAlpha = FMath::Clamp(SpeedAlpha, 0.f, 1.f);
	AnimTime += DeltaSeconds * (0.8f + Energy) * (1.f + SpeedAlpha * 1.5f + Excite);

	// Body bob / squash & stretch.
	const float Wave = FMath::Sin(AnimTime * 4.f);
	float BodyLift = HoverHeight;
	float Squash = 0.f;
	if (bHovers)
	{
		BodyLift += Wave * ModelHeight * 0.03f * (1.f + Excite);
		BodyPivot->SetRelativeLocation(FVector(0.f, 0.f, BodyLift));
	}
	else
	{
		Squash = Wave * (0.025f + 0.04f * SpeedAlpha + 0.03f * Excite);
		BodyPivot->SetRelativeScale3D(FVector(1.f - Squash * 0.5f, 1.f - Squash * 0.5f, 1.f + Squash));

		if (FootL && FootR)
		{
			const float Step = AnimTime * 6.f;
			const float Lift = ModelHeight * 0.06f * SpeedAlpha;
			FootL->SetRelativeLocation(FootLBase + FVector(FMath::Cos(Step) * Lift, 0.f, FMath::Max(0.f, FMath::Sin(Step)) * Lift));
			FootR->SetRelativeLocation(FootRBase + FVector(FMath::Cos(Step + PI) * Lift, 0.f, FMath::Max(0.f, FMath::Sin(Step + PI)) * Lift));
		}
	}

	// Head follows the top of the body.
	HeadPivot->SetRelativeLocation(FVector(0.f, 0.f, HeadBaseZ * (1.f + Squash) + BodyLift));

	// Head look-at (clamped), otherwise a lazy idle sway.
	FRotator Desired(0.f, FMath::Sin(AnimTime * 0.6f) * 12.f, FMath::Sin(AnimTime * 0.9f) * 4.f);
	if (LookTarget)
	{
		const FVector Local = GetComponentTransform().InverseTransformPosition(*LookTarget) - HeadPivot->GetRelativeLocation();
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
		const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(Local.Z, FMath::Max(1.f, Local.Size2D())));
		Desired = FRotator(FMath::Clamp(Pitch, -30.f, 35.f), FMath::Clamp(Yaw, -70.f, 70.f), 0.f);
	}
	HeadRotation = FMath::RInterpTo(HeadRotation, Desired, DeltaSeconds, 5.f + 5.f * Excite);
	HeadPivot->SetRelativeRotation(HeadRotation);

	// Antenna sway.
	for (int32 i = 0; i < AntennaPivots.Num(); ++i)
	{
		if (AntennaPivots[i])
		{
			const float Amp = 6.f + 10.f * Excite + 6.f * Energy;
			AntennaPivots[i]->SetRelativeRotation(FRotator(
				FMath::Sin(AnimTime * 3.1f + i) * Amp,
				0.f,
				AntennaBaseRoll[i] + FMath::Sin(AnimTime * 2.3f + i * 1.7f) * Amp * 0.6f));
		}
	}

	// Blink.
	float EyeOpen = 1.f;
	if (BlinkPhase < 0.f)
	{
		BlinkTimer -= DeltaSeconds;
		if (BlinkTimer <= 0.f)
		{
			BlinkPhase = 0.f;
		}
	}
	if (BlinkPhase >= 0.f)
	{
		BlinkPhase += DeltaSeconds;
		const float Half = 0.08f;
		EyeOpen = BlinkPhase < Half ? 1.f - 0.9f * (BlinkPhase / Half) : 0.1f + 0.9f * FMath::Min(1.f, (BlinkPhase - Half) / Half);
		if (BlinkPhase > Half * 2.f)
		{
			BlinkPhase = -1.f;
			BlinkTimer = FMath::FRandRange(2.f, 5.f);
			EyeOpen = 1.f;
		}
	}
	for (int32 i = 0; i < Eyes.Num(); ++i)
	{
		if (Eyes[i])
		{
			const FVector& Base = EyeBaseScales[i];
			Eyes[i]->SetRelativeScale3D(FVector(Base.X, Base.Y, Base.Z * EyeOpen));
		}
	}
}
