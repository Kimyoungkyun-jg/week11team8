#pragma once

#include "Components/MovementComponent.h"
#include "CharacterMovementComponent.generated.h"

enum class ECharacterMovementMode : uint8 { Walking, Falling };

UCLASS()
class UCharacterMovementComponent : public UMovementComponent
{
	GENERATED_BODY()
public:
	UCharacterMovementComponent();
	void TickComponent(float DeltaTime) override;
	void RequestJump() { bJumpRequested = true; }
	void StopJumping() { bJumpRequested = false; }
	bool IsMovingOnGround() const { return MovementMode == ECharacterMovementMode::Walking; }
	ECharacterMovementMode GetMovementMode() const { return MovementMode; }
	void StopMovementImmediately() override;

	UPROPERTY(EditAnywhere)
	float MaxWalkSpeed = 5.0f;
	UPROPERTY(EditAnywhere)
	float MaxAcceleration = 40.0f;
	UPROPERTY(EditAnywhere)
	float BrakingDecelerationWalking = 40.0f;
	UPROPERTY(EditAnywhere)
	float AirControl = 0.35f;
	UPROPERTY(EditAnywhere)
	float GravityScale = 1.0f;
	UPROPERTY(EditAnywhere)
	float JumpZVelocity = 4.2f;
	UPROPERTY(EditAnywhere)
	float WalkableFloorAngle = 45.0f;

private:
	bool FindFloor(float Distance, FHitResult& OutHit) const;
	bool SweepCapsule(const FVector& Delta, FHitResult& OutHit) const;
	ECharacterMovementMode MovementMode = ECharacterMovementMode::Falling;
	bool bJumpRequested = false;
};
