// Alien Museum - an alien's home-world diorama, built inside its display case.
//
// Create one per world (Content Browser > Miscellaneous > Data Asset > ChamberHabitatAsset) and assign
// it to an alien's data asset. UChamberHabitatComponent turns it into a ground layer, props (loose
// physics props the alien can knock around, decorations, obstacles) and a living ambient effect.
// Sizes are centimetres at chamber scale 1; props are placed at random but repeatably per chamber.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/AlienDataAsset.h"
#include "ChamberHabitatAsset.generated.h"

/** What covers the floor of the case. */
UENUM(BlueprintType)
enum class EHabitatGround : uint8
{
	None UMETA(ToolTip = "Keep the case's own glowing floor"),
	Sand,
	Rock,
	Soil,
	Grass,
	Organic,
	Crystal UMETA(ToolTip = "Dark glassy ground with a faint glow"),
	Tech UMETA(ToolTip = "Dark metal floor"),
	Lava UMETA(ToolTip = "Glowing lava with flowing cracks"),
	Water UMETA(ToolTip = "A pool: Ground Depth deep, props sink to the bottom")
};

/** A small moving effect that keeps the diorama alive. */
UENUM(BlueprintType)
enum class EHabitatAmbient : uint8
{
	None,
	Embers UMETA(ToolTip = "Glowing sparks rising and fading"),
	Bubbles UMETA(ToolTip = "Bubbles rising through the water"),
	Mist UMETA(ToolTip = "Soft mist drifting over the ground"),
	Sparkles UMETA(ToolTip = "Twinkling points of light"),
	Spores UMETA(ToolTip = "Dust / pollen floating slowly upwards"),
	Pulses UMETA(ToolTip = "Glowing lines lighting up across a tech floor")
};

/** Where in the case a prop goes. */
UENUM(BlueprintType)
enum class EHabitatPlacement : uint8
{
	Scatter UMETA(ToolTip = "Anywhere on the floor, keeping the middle clear"),
	Edges UMETA(ToolTip = "Along the glass"),
	Corners,
	Back UMETA(ToolTip = "Along the back wall (away from the viewer's side)")
};

UENUM(BlueprintType)
enum class EHabitatPropLook : uint8
{
	Lit UMETA(ToolTip = "Normal lit surface"),
	Glow UMETA(ToolTip = "Glows in its colour"),
	Crystal UMETA(ToolTip = "See-through glowing crystal")
};

USTRUCT(BlueprintType)
struct BEN10_API FHabitatProp
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
	EAlienPartShape Shape = EAlienPartShape::Sphere;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
	FLinearColor Color = FLinearColor(0.4f, 0.38f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
	EHabitatPropLook Look = EHabitatPropLook::Lit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop", meta = (ClampMin = 0, ClampMax = 40))
	int32 Count = 3;

	/** Size range (cm): the diameter of spheres / cylinders / cones, the edge of cubes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
	FVector2D Size = FVector2D(4.f, 8.f);

	/** Per-axis stretch of the base size (e.g. Z = 4 for a tall crystal, X = 3 for a log). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
	FVector Stretch = FVector::OneVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
	EHabitatPlacement Placement = EHabitatPlacement::Scatter;

	/** Random tilt (degrees) away from upright. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop", meta = (ClampMin = 0, ClampMax = 90))
	float Tilt = 0.f;

	/** Loose: falls, rolls and gets pushed around by the alien (real physics). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop")
	bool bPhysics = false;

	/** A fixed prop the alien walks around instead of through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop", meta = (EditCondition = "!bPhysics"))
	bool bBlocksAlien = false;

	/** Plants: sway gently. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prop", meta = (EditCondition = "!bPhysics"))
	bool bSway = false;
};

UCLASS(BlueprintType)
class BEN10_API UChamberHabitatAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Habitat")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground")
	EHabitatGround Ground = EHabitatGround::Sand;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground")
	FLinearColor GroundColor = FLinearColor(0.55f, 0.45f, 0.3f);

	/** Lava glow / water colour / crystal and tech glow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground")
	FLinearColor GroundGlowColor = FLinearColor(1.f, 0.35f, 0.05f);

	/** Thickness of the ground layer (cm); for Water the depth of the pool. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = 0.5, ClampMax = 60))
	float GroundDepth = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient")
	EHabitatAmbient Ambient = EHabitatAmbient::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient")
	FLinearColor AmbientColor = FLinearColor(1.f, 0.5f, 0.1f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ambient", meta = (ClampMin = 0, ClampMax = 30))
	int32 AmbientCount = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Props")
	TArray<FHabitatProp> Props;
};
