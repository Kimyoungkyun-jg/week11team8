#pragma once

#include "GameFramework/Character.h"
#include "TestCharacter.generated.h"

class ARifleActor;
class UUserWidget;

UCLASS()
class ATestCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	ATestCharacter();
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void EndPlay() override;
	void Jump() override;
	void Attack() override;
	void StartAim();
	void StopAim();
	void LockJumpUntilLandingAnimation() { bJumpLocked = true; }
	void OnLandingAnimationFinished() { bJumpLocked = false; }
	void OnAttackAnimationStarted() { bAttackLocked = true; }
	void OnAttackAnimationFinished() { bAttackLocked = false; }
private:
	ARifleActor* EquippedRifle = nullptr;
	int32 RightHandBoneIndex = -1;
	int32 LeftHandBoneIndex = -1;
	UPROPERTY(EditAnywhere)
	FVector RifleGripOffset = FVector(0.10f, 0.0f, 0.08f);
	UPROPERTY(EditAnywhere)
	FRotator RifleGripRotation = FRotator(0.0f, 0.0f, 0.0f);
	bool bJumpLocked = false;
	bool bAttackLocked = false;
	bool bIsAiming = false;
	float AimBlendAlpha = 0.0f;
	float NormalArmLength = 2.8f;
	float NormalFOV = 60.0f;
	UPROPERTY(EditAnywhere)
	float AimArmLength = 1.5f;
	UPROPERTY(EditAnywhere)
	float AimFOV = 70.0f;
	UUserWidget* AimWidget = nullptr;
};
