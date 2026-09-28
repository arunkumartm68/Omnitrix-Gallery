// Alien Museum - builds and animates the alien body from an AlienDataAsset.
//
// The body is either the data asset's imported model (ModelMesh: one static mesh, scaled to the
// alien's height and stood on its feet) or made of engine basic shapes (spheres, cylinders, cones,
// cubes): one of the built-in shapes (Blob / Tall / Squat) or fully described by the Parts list
// (BodyShape = Custom). Animation is procedural (bob, squash & stretch, walk waddle / swing, head
// look-at, blink, wing flaps, flame flicker; models lean into turns and when speeding up), which is
// far cheaper on Quest than skeletal meshes + animation blueprints. Signature moves
// (UAlienActionComponent) bend the whole body through the action transform and extra lift.
// A rigged model (the data asset's RiggedMesh, a skinned copy of the model) is animated bone by bone
// from the data asset's Rig: legs walk with their paws planted (two-bone IK), the body bobs, sways and
// breathes, the head looks around and sniffs, a tail waves. A model with its own animations (the data
// asset's Clips: Benwolf) plays them instead of the rest pose - idle and walk blended by speed, a howl
// or a flinch on top when asked - and the Rig's head look works on top of them.
// A Blueprint subclass of AAlienCharacter with a skeletal mesh also works; this component then stays empty.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Data/AlienDataAsset.h"
#include "AlienAppearanceComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class USkeletalMesh;
class UPoseableMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UCLASS(ClassGroup = (AlienMuseum), meta = (BlueprintSpawnableComponent))
class BEN10_API UAlienAppearanceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UAlienAppearanceComponent();

	/** (Re)builds the body parts. The component origin is the alien's feet. */
	UFUNCTION(BlueprintCallable, Category = "Alien")
	void BuildAppearance(const UAlienDataAsset* Data);

	/** Removes all generated parts. */
	UFUNCTION(BlueprintCallable, Category = "Alien")
	void ClearAppearance();

	/**
	 * Advances the procedural animation. Call once per frame from the owner.
	 * @param SpeedAlpha  0 = standing, 1 = walking at full speed.
	 * @param LookTarget  Optional world-space point the head should look at.
	 * @param bExcited    Faster, bigger motion (reacting to the player).
	 */
	void UpdateAnimation(float DeltaSeconds, float SpeedAlpha, const FVector* LookTarget, bool bExcited);

	/** Starts the "materialize" pop-in (scale from nothing with a little overshoot). */
	void PlayMaterialize(float Duration = 0.6f);

	/** Advances the materialize effect. Returns true while it is still playing. */
	bool UpdateMaterialize(float DeltaSeconds);

	/** Height of the model at scale 1 (cm). */
	float GetModelHeight() const { return ModelHeight; }

	/** Approximate horizontal radius of the model at scale 1 (cm). */
	float GetModelRadius() const { return ModelRadius; }

	/** How far the body reaches forward of its centre (cm at scale 1): its chest, its snout. */
	float GetModelFront() const { return ModelFront > 0.f ? ModelFront : ModelRadius; }

	/** Radius for the collision capsule at scale 1 (cm). */
	float GetCollisionRadius() const { return CollisionRadius; }

	/** Radius of the fake contact shadow at scale 1 (cm): the feet area, not outstretched arms. */
	float GetShadowRadius() const { return ShadowRadius; }

	bool HasParts() const { return Parts.Num() > 0; }

	/** True when the body is an imported model rather than basic shapes. */
	bool IsModel() const { return bIsModel; }

	/** Where the body's feet sit relative to the parent (the owner places it). Moves build on it. */
	void SetBaseLocation(const FVector& Location);

	/** Extra scale / rotation / offset of the whole body for signature moves (pivot: the feet). */
	void SetActionTransform(const FVector& Scale, const FRotator& Rotation = FRotator::ZeroRotator, const FVector& Offset = FVector::ZeroVector);
	void ClearActionTransform() { SetActionTransform(FVector::OneVector); }

	/** Lifts the whole body off the ground (flying). The capsule and the contact shadow stay down. */
	void SetExtraLift(float Lift);
	float GetExtraLift() const { return ExtraLift; }

	/** Hides / shows the body parts (effects attached to them are left alone). */
	void SetBodyVisible(bool bShow);
	bool IsBodyVisible() const { return bBodyVisible; }

	/** The imported model's mesh component (nullptr for shape-built bodies). Hidden when the model is
	 *  rigged: it then only gives the after-images their shape and place. */
	UStaticMeshComponent* GetModelComponent() const { return ModelComponent; }

	/** True when the body is a rigged model animated bone by bone. */
	bool IsRigged() const { return RigComponent != nullptr; }

	/** True when the rigged model plays its own clips (the data asset's Clips). */
	bool HasClips() const { return RigClips.Num() > 0; }

	bool HasClip(EAlienClip Clip) const { return RigClips.IsValidIndex(static_cast<int32>(Clip)) && RigClips[static_cast<int32>(Clip)] != nullptr; }

	/**
	 * Plays one of the model's clips over the idle / walk (a howl, a flinch), fading it in. A looping clip
	 * plays until StopClip; a clip started while another plays at full strength follows on without a fade
	 * (a wind-up, then its loop). Returns the clip's length in seconds (0 when the model has no such clip).
	 */
	float PlayClip(EAlienClip Clip, bool bLoop = false, float BlendTime = 0.2f, float Rate = 1.f);

	/** Fades the playing clip out, back to the idle / walk. */
	void StopClip(float BlendTime = 0.25f);

	/** A clip is playing over the idle / walk (and not fading out). */
	bool IsPlayingClip() const { return ActiveClip != nullptr && !bClipFadingOut; }

	/** World position of the rig's head bone (false when the body has no rigged head). */
	bool GetHeadBoneLocation(FVector& OutLocation) const;

	/**
	 * Up on the hind legs with the front paws / hands on two points in the world (Wildmutt's paws on the
	 * glass): Weight 0 stands and walks as usual, 1 has each front leg reach for its point, the body pitched
	 * up around the hips and stepping forward as far as the paws need. Rigged bodies with front legs only
	 * (see CanReachFront).
	 */
	void SetFrontReach(const FVector& LeftPoint, const FVector& RightPoint, float Weight);
	void ClearFrontReach() { FrontReachWeight = 0.f; }
	bool CanReachFront() const;

	/** A sniff now (nose up, quick short sniffs), if the rig sniffs. */
	void Sniff();

	/** Shows a second pose of the model (same scale, feet on the ground) until EndPose(). */
	bool ShowPose(UStaticMesh* PoseMesh);
	void EndPose();

	/** The component head effects (flames) attach to, and the top of the head relative to it. */
	USceneComponent* GetHeadTop(FVector& OutOffset) const;

	/** Optional material overrides (default: /Game/AlienMuseum/Materials). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alien")
	TObjectPtr<UMaterialInterface> SkinMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alien")
	TObjectPtr<UMaterialInterface> GlowMaterialOverride;

	/** Dynamic shadows on the body parts. Off by default: a contact shadow is much cheaper on Quest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alien")
	bool bCastShadows = false;

	/** Use the data asset's rigged (skinned, animated) model when it has one. Off for the collection's
	 *  card previews: the static model looks the same there and costs far less. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alien")
	bool bUseRiggedModel = true;

private:
	/** A data part that moves on its own (swinging limb, flapping wing, flickering flame...). */
	struct FAnimatedPart
	{
		TWeakObjectPtr<UStaticMeshComponent> Component;
		FVector BaseLocation = FVector::ZeroVector;
		FQuat BaseRotation = FQuat::Identity;
		FVector BaseScale = FVector::OneVector;
		FVector Pivot = FVector::ZeroVector; // parent space
		EAlienPartMotion Motion = EAlienPartMotion::None;
		float Amount = 0.f;
		float Speed = 1.f;
		float Phase = 0.f;
	};

	void CreateMaterials(const UAlienDataAsset* Data);
	void BuildBuiltInBody(const UAlienDataAsset* Data);
	void BuildModelBody(const UAlienDataAsset* Data, UStaticMesh* Mesh);
	void AddDataPart(const FAlienBodyPart& Part, bool bMirrored);
	UMaterialInterface* GetPartMaterial(const FAlienBodyPart& Part);
	UStaticMesh* GetShapeMesh(EAlienPartShape Shape) const;
	void AnimateDataParts(float SpeedAlpha);
	void ApplyRootTransform();
	void BuildRig(const UAlienDataAsset* Data, USkeletalMesh* Mesh);
	void UpdateRig(float DeltaSeconds, float TurnAlpha, float Excite);
	void AddRigTurn(int32 Bone, const FQuat& Turn);

	/** This frame's base pose from the clips (RigBase): idle / walk by ground speed (cm/s at scale 1), mid-air, the playing clip. */
	void UpdateClipBase(float DeltaSeconds, float GroundSpeed);

	/** A clip's pose at Time, bone space, in the mesh's bone order. */
	void SampleClip(const UAnimSequence* Clip, float Time, TArray<FTransform>& Out) const;

	UStaticMeshComponent* AddPart(const TCHAR* BaseName, UStaticMesh* Mesh, USceneComponent* Parent, const FTransform& Relative, UMaterialInterface* Material);
	USceneComponent* AddPivot(const TCHAR* BaseName, USceneComponent* Parent, const FTransform& Relative);

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Parts;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> BodyPivot;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> HeadPivot;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> AntennaPivots;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Eyes;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ModelComponent;

	/** The rigged model (skinned, posed bone by bone every frame). */
	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> RigComponent;

	/** The model's normal pose (while ShowPose swaps the mesh). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> RestMesh;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FootL;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> FootR;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SkinMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AccentMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DarkMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EyeMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowMID;

	/** One material instance per custom colour. */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> CustomMIDs;

	TArray<FAnimatedPart> AnimatedParts;

	/** A model part that swings around its hinge (wings). */
	struct FModelPartMotion
	{
		TWeakObjectPtr<USceneComponent> Pivot;
		FVector Axis = FVector::ForwardVector; // body pivot space
		float Amount = 15.f;
		float Offset = 0.f;
		float Speed = 6.f;
		float FlyingAmount = 32.f;
		float FlyingOffset = 0.f;
		float FlyingSpeed = 12.f;
		float Phase = 0.f;
		float Cycle = 0.f;                     // own clock, so speed changes never jump
	};
	TArray<FModelPartMotion> ModelPartMotions;

	/** A walking leg of the rig: bone indices and its rest geometry in component space. */
	struct FRigLeg
	{
		int32 Upper = INDEX_NONE;
		int32 Lower = INDEX_NONE;
		int32 End = INDEX_NONE;
		FVector Home = FVector::ZeroVector;   // where the paw stands in the rest pose
		FVector Bend = FVector::ZeroVector;   // the way the elbow / knee points
		float UpperLength = 1.f;
		float LowerLength = 1.f;
		float Phase = 0.f;
		float PawLength = 1.f;               // wrist above the ground in the rest pose
		bool bFront = false;
		bool bLeft = false;
	};
	FAlienRig RigSettings;
	TArray<FRigLeg> RigLegs;
	TArray<int32> RigParents;
	TArray<FTransform> RigRestLocal;   // rest pose, bone space
	TArray<FTransform> RigRestSpace;   // rest pose, component space
	TArray<FTransform> RigPose;        // this frame, bone space (written to the component)
	TArray<FTransform> RigSpace;       // this frame, component space
	TArray<FQuat> RigTurns;            // this frame's extra turn of each bone, component space
	TArray<int32> RigSpine;
	TArray<int32> RigTail;
	TArray<int32> RigFloating;
	int32 RigNeck = INDEX_NONE;
	int32 RigHead = INDEX_NONE;
	int32 RigJaw = INDEX_NONE;
	FVector RigTailDirection = FVector::DownVector;
	float RigHeight = 1.f;             // model height in the rig's own units
	float GaitCycle = 0.f;             // 0..1 through the step cycle
	float GaitMoving = 0.f;            // 0 standing still .. 1 stepping (smoothed)
	float Airborne = 0.f;              // 0 on the ground .. 1 leaping or held (smoothed)
	float RigBreathCycle = 0.f;
	float RigTailCycle = 0.f;
	float SniffTimer = 3.f;
	float SniffTime = -1.f;
	float RigCrouch = 0.f;             // a move's squash, done as bent legs
	float RigNod = 0.f;                // a move's pitch while on the ground, done by the head
	FVector FrontReachPoints[2] = { FVector::ZeroVector, FVector::ZeroVector }; // world: left, right
	float FrontReachWeight = 0.f;      // as asked
	float FrontReach = 0.f;            // smoothed
	float RigRear = 0.f;               // degrees the body is pitched up around the hips (smoothed)
	float RigReachShift = 0.f;         // how far the body has stepped forward to reach (rig units, smoothed)
	float ModelMeshHeight = 1.f;       // the model mesh's own height (before scaling)

	/** The model's own clips, by EAlienClip (empty when it has none). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> RigClips;

	/** The clip playing over the idle / walk. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> ActiveClip;

	TArray<int32> RigClipBones;        // mesh bone -> skeleton bone (the clips' tracks)
	TArray<FTransform> RigBase;        // this frame's pose before the turns, bone space (clip models)
	TArray<FTransform> RigClipScratch;
	float ClipMoveSpeed = 60.f;        // cm/s at scale 1 where the walk clip plays at its own pace
	float ClipIdleTime = 0.f;
	float ClipMoveTime = 0.f;
	float ClipMoveWeight = 0.f;
	float ActiveClipTime = 0.f;
	float ActiveClipRate = 1.f;
	float ActiveClipWeight = 0.f;
	float ClipFadeIn = 0.2f;
	float ClipFadeOut = 0.25f;
	bool bActiveClipLoops = false;
	bool bClipFadingOut = false;

	float ModelHeight = 32.f;
	float ModelRadius = 10.f;
	float ModelFront = 0.f;
	float CollisionRadius = 8.f;
	float ShadowRadius = 10.f;
	float Energy = 0.5f;
	bool bIsModel = false;
	bool bHovers = false;
	float HoverHeight = 0.f;
	FVector2D HeadBaseXY = FVector2D::ZeroVector;
	float HeadBaseZ = 0.f;
	float HeadTopOffset = 0.f;   // top of the head above the head pivot (shape bodies)

	FQuat ModelFix = FQuat::Identity;
	FVector ModelOffset = FVector::ZeroVector;
	float ModelScale = 1.f;

	// Whole-body transform: owner placement + materialize pop-in + signature moves.
	FVector BaseLocation = FVector::ZeroVector;
	bool bHasBaseLocation = false;
	float MaterializeScale = 1.f;
	FVector ActionScale = FVector::OneVector;
	FRotator ActionRotation = FRotator::ZeroRotator;
	FVector ActionOffset = FVector::ZeroVector;
	float ExtraLift = 0.f;
	bool bBodyVisible = true;

	// Weight: models lean into turns and pitch when they speed up or slow down.
	float LastParentYaw = 0.f;
	bool bHasLastYaw = false;
	float LastSpeedAlpha = 0.f;
	float LeanRoll = 0.f;
	float LeanPitch = 0.f;

	FVector FootLBase = FVector::ZeroVector;
	FVector FootRBase = FVector::ZeroVector;
	TArray<FVector> EyeBaseScales;
	TArray<float> AntennaBaseRoll;

	float AnimTime = 0.f;
	float BreathTime = 0.f;
	float MaterializeTime = -1.f;
	float MaterializeDuration = 0.6f;
	float BlinkTimer = 2.f;
	float BlinkPhase = -1.f;
	FRotator HeadRotation = FRotator::ZeroRotator;
};
