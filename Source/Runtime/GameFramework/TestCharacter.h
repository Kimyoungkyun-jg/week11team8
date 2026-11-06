#pragma once

#include "GameFramework/Character.h"
#include "TestCharacter.generated.h"

class UStaticMeshComponent;
class USpringArmComponent;

UCLASS()
class ATestCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	ATestCharacter();
	void BeginPlay() override;
	void AddControllerPitchInput(float Value) override;

	UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }
	USpringArmComponent* GetSpringArmComponent() const { return SpringArmComponent; }

private:
	UStaticMeshComponent* StaticMeshComponent = nullptr;
	USpringArmComponent* SpringArmComponent = nullptr;
};
