#pragma once

#include "GameFramework/Pawn.h"
#include "Character.generated.h"

class UCapsuleComponent;
class UCameraComponent;
class UCharacterMovementComponent;

UCLASS()
class ACharacter : public APawn
{
	GENERATED_BODY()
public:
	ACharacter();
	void BeginPlay() override;
	void AddMovementInput(const FVector& WorldDirection, float ScaleValue) override;
	void AddControllerYawInput(float Value) override;
	void AddControllerPitchInput(float Value) override;
	void Jump() override;
	void StopJumping() override;
	UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }
	UCameraComponent* GetCameraComponent() const { return CameraComponent; }
	UCharacterMovementComponent* GetCharacterMovement() const { return CharacterMovement; }
private:
	UCapsuleComponent* CapsuleComponent = nullptr;
	UCameraComponent* CameraComponent = nullptr;
	UCharacterMovementComponent* CharacterMovement = nullptr;
};
