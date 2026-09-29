// Alien Museum - the classic Omnitrix on the player's left wrist.
//
// Look at it and it wakes: the green hourglass lights up. Press it - click the left thumbstick, or poke its
// side button - and the core pops up as the dial: turn it (left thumbstick left / right, or pinch the core
// and twist) through the aliens' silhouettes, the name shown above the watch, and slam it down (left
// trigger, or push the core down with a finger) to go alien: OnTransform (step 5 brings the alien out,
// life-size). The alien lasts TransformTime. In the last WarningTime the watch beeps, faster and faster,
// and flashes red; then it times out (OnRevert) and recharges - red, refusing to work - for RechargeTime,
// until it chimes ready. Pressed while transformed, it turns back early (a short recharge).
//
// The watch rides on the controller's grip pose (late-updated with it), on the tracked wrist joint, or on
// the desktop at the bottom left of the view. Its geometry comes from the imported model
// (Models/Omnitrix/Band|Core|Face, see Scripts/import_omnitrix.py): X is the side button's side, Y runs
// along the forearm, Z is out of the face.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OmnitrixWatch.generated.h"

class UAlienDataAsset;
class UCameraComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMuseumHandInteractor;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;
class AMuseumDirector;

UENUM(BlueprintType)
enum class EOmnitrixState : uint8
{
	Asleep,       // a dim green hourglass
	Awake,        // looked at: the hourglass lights up
	Dial,         // the core is up: silhouettes, the name above the watch
	Transformed,  // an alien is out: a ring on the face shows the time left
	Recharging    // red, refuses to work
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOmnitrixTransformSignature, UAlienDataAsset*, Alien);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOmnitrixRevertSignature, bool, bTimedOut);

UCLASS(ClassGroup = (AlienMuseum))
class BEN10_API AOmnitrixWatch : public AActor
{
	GENERATED_BODY()

public:
	AOmnitrixWatch();

	/** Worn on WristHand; OtherHand's fingers work it. Camera: the player's eyes (and the desktop placement). */
	void Setup(UMuseumHandInteractor* InWristHand, UMuseumHandInteractor* InOtherHand, UCameraComponent* InCamera);

	virtual void Tick(float DeltaSeconds) override;

	// ---- Controls: the pawn routes the buttons, the hand gestures are read here ----

	/** The button (a thumbstick click, a poke on the side button): pops the dial up or closes it; turns back. */
	UFUNCTION(BlueprintCallable, Category = "Omnitrix")
	void Press();

	/** Turns the dial by Steps aliens. */
	UFUNCTION(BlueprintCallable, Category = "Omnitrix")
	void Dial(int32 Steps);

	/** A thumbstick's left / right (-1..1): a push turns the dial one step, holding it keeps turning. 0 = let go. */
	void DialStick(float Value);

	/** Slams the core down: the selected alien. */
	UFUNCTION(BlueprintCallable, Category = "Omnitrix")
	void Slam();

	UFUNCTION(BlueprintPure, Category = "Omnitrix")
	EOmnitrixState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Omnitrix")
	bool IsDialOpen() const { return State == EOmnitrixState::Dial; }

	/** The alien the dial shows. */
	UFUNCTION(BlueprintPure, Category = "Omnitrix")
	UAlienDataAsset* GetSelectedAlien() const;

	/** The alien it turned into (null when not transformed). */
	UFUNCTION(BlueprintPure, Category = "Omnitrix")
	UAlienDataAsset* GetActiveAlien() const { return ActiveAlien; }

	/** Seconds until it times out (transformed) or is ready again (recharging); 0 otherwise. */
	UFUNCTION(BlueprintPure, Category = "Omnitrix")
	float GetTimeLeft() const;

	/** Hand is working the watch up close (the pawn then keeps its buttons and pointer off everything else). */
	bool WantsHand(const UMuseumHandInteractor* Hand) const;

	/** Transformed: the alien to bring out. */
	UPROPERTY(BlueprintAssignable, Category = "Omnitrix")
	FOmnitrixTransformSignature OnTransform;

	/** Back to normal: the time ran out (bTimedOut) or the player turned back. */
	UPROPERTY(BlueprintAssignable, Category = "Omnitrix")
	FOmnitrixRevertSignature OnRevert;

	// ---- Settings ----

	/** How long an alien lasts (the museum-friendly 3 minutes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix", meta = (Units = "s"))
	float TransformTime = 180.f;

	/** The last seconds of it: beeping, flashing red. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix", meta = (Units = "s"))
	float WarningTime = 20.f;

	/** After timing out it refuses to work this long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix", meta = (Units = "s"))
	float RechargeTime = 20.f;

	/** ... and this long after the player turned back early. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix", meta = (Units = "s"))
	float TurnBackRechargeTime = 5.f;

	/** An open dial nobody touches closes again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix", meta = (Units = "s"))
	float DialTimeout = 12.f;

	/** The model is a child's watch: this makes it fit an adult's wrist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix")
	float WatchScale = 1.9f;

	/** It wakes when its face is turned to the eyes within this angle, and in view, and near. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix", meta = (Units = "deg"))
	float WakeAngle = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix", meta = (Units = "cm"))
	float WakeDistance = 80.f;

	/** Pinch the core and turn it this far for one step of the dial. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix", meta = (Units = "deg"))
	float DialStepAngle = 30.f;

	/** A finger pushing the popped-up core down at least this fast slams it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix", meta = (Units = "cm/s"))
	float SlamSpeed = 30.f;

	/** Where the watch sits on the tracked wrist joint (X along the hand to the fingers, Z out of its back). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix|Placement")
	FTransform HandOffset;

	/** ... on the controller's grip pose (X forward through the controller, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix|Placement")
	FTransform ControllerOffset;

	/** ... on the desktop, relative to the camera. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix|Placement")
	FTransform DesktopOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix|Look")
	FLinearColor ReadyColor = FLinearColor(0.25f, 1.f, 0.2f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix|Look")
	FLinearColor WarningColor = FLinearColor(1.f, 0.08f, 0.04f);

	/** Nothing to dial (an empty collection). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Omnitrix|Look")
	FLinearColor RefuseColor = FLinearColor(1.f, 0.75f, 0.05f);

protected:
	virtual void BeginPlay() override;

private:
	void SetState(EOmnitrixState NewState);
	void OpenDial();
	void CloseDial();
	void TurnBack(bool bTimedOut);
	void Refuse();
	void UpdatePlacement();
	void UpdateLook(float DeltaSeconds);
	void UpdateTimer(float DeltaSeconds);
	void UpdateGestures(float DeltaSeconds);
	void UpdateVisuals(float DeltaSeconds);
	void ShowSelection();
	void Sound(FName Name, float Volume = 1.f);
	void Buzz(float Amplitude, float Duration);
	TArray<UAlienDataAsset*> GetAliens() const;
	FVector FaceCentreWorld() const;

	UPROPERTY(VisibleAnywhere, Category = "Omnitrix")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Omnitrix")
	TObjectPtr<UStaticMeshComponent> Band;

	/** The core and the face on it: rise for the dial, turn with it. */
	UPROPERTY(VisibleAnywhere, Category = "Omnitrix")
	TObjectPtr<USceneComponent> CorePivot;

	UPROPERTY(VisibleAnywhere, Category = "Omnitrix")
	TObjectPtr<UStaticMeshComponent> Core;

	UPROPERTY(VisibleAnywhere, Category = "Omnitrix")
	TObjectPtr<UStaticMeshComponent> Face;

	/** The dialled alien's name, above the watch. */
	UPROPERTY(VisibleAnywhere, Category = "Omnitrix")
	TObjectPtr<UTextRenderComponent> Label;

	/** The transformation's green flash in front of the eyes. */
	UPROPERTY(VisibleAnywhere, Category = "Omnitrix")
	TObjectPtr<UStaticMeshComponent> FlashCard;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FaceMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FaceMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlashMID;

	UPROPERTY(Transient)
	TObjectPtr<UMuseumHandInteractor> WristHand;

	UPROPERTY(Transient)
	TObjectPtr<UMuseumHandInteractor> OtherHand;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(Transient)
	TObjectPtr<UAlienDataAsset> ActiveAlien;

	TWeakObjectPtr<AMuseumDirector> Director;
	TWeakObjectPtr<USceneComponent> AttachedTo;

	EOmnitrixState State = EOmnitrixState::Asleep;
	float StateTime = 0.f;
	float TimeLeft = 0.f;         // transformed: until the time-out; recharging: until ready
	float RechargeLength = 1.f;   // this recharge's full length
	int32 Selected = 0;
	float LastInputTime = 0.f;    // dial: seconds since it was last turned
	bool bSettled = true;         // dial: the chirp for the alien it stopped on has played
	bool bShown = false;
	bool bLookedAt = false;
	float LookTime = 0.f;
	float AwayTime = 0.f;
	double LastWakeSound = -100.0;

	// Look
	float Pop = 0.f;              // 0 down .. 1 up (smoothed)
	float CoreTurn = 0.f;         // degrees (smoothed)
	float CoreTurnTarget = 0.f;
	float Brightness = 0.2f;      // smoothed
	float Flash = 0.f;            // 1 -> 0: white-green flash on the face
	FLinearColor FlashColor = FLinearColor::White;
	float ScreenFlash = 0.f;      // 1 -> 0: the flash in front of the eyes
	float BeepTimer = 0.f;
	float BeepFlash = 0.f;
	float RefuseBlink = 0.f;

	// Stick
	int32 StickDirection = 0;
	double NextStickStep = 0.0;

	// Gestures
	bool bButtonArmed = true;
	bool bTwisting = false;
	bool bWasPinching = false;
	float TwistAngle = 0.f;
	float TwistTurned = 0.f;
	bool bSlamArmed = false;
	float LastAbove = 100.f;
};
