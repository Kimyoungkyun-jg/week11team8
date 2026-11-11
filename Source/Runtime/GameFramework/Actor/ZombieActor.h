#pragma once

#include "GameFramework/Character.h"
#include "ZombieActor.generated.h"

class UCapsuleComponent;
class UPrimitiveComponent;

UCLASS()
class AZombieActor : public ACharacter
{
	GENERATED_BODY()
public:
	AZombieActor();
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void ReactToHit();
	bool SetCombatTuning(float InDetectionRadius, float InAttackRadius,
		float InAttackCooldown, float InWalkSpeed);
	ACharacter* GetTarget() const { return Target; }

private:
	void OnRightHandOverlap(UPrimitiveComponent* Self, AActor* OtherActor, UPrimitiveComponent* OtherComponent);
	UCapsuleComponent* RightHandHitCapsule = nullptr;
	int32 RightHandBoneIndex = -1;
	ACharacter* Target = nullptr;
	float AttackTimer = 0.0f;

	UPROPERTY(EditAnywhere)
	float DetectionRadius = 12.0f;
	UPROPERTY(EditAnywhere)
	float AttackRadius = 2.0f;
	UPROPERTY(EditAnywhere)
	float AttackCooldown = 1.8f;
};
