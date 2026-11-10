#pragma once

#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "RifleActor.generated.h"

class AProjectileActor;

UCLASS()
class ARifleActor : public AActor
{
	GENERATED_BODY()
public:
	ARifleActor();
	void EndPlay() override;

	AProjectileActor* Fire();
	UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }
	const TArray<AProjectileActor*>& GetProjectiles() const { return Projectiles; }

private:
	UStaticMeshComponent* StaticMeshComponent = nullptr;
	TArray<AProjectileActor*> Projectiles;

	UPROPERTY(EditAnywhere)
	float MuzzleDistance = 0.52f;
	UPROPERTY(EditAnywhere)
	float ProjectileSpeed = 80.0f;
	UPROPERTY(EditAnywhere)
	int32 MaxProjectiles = 16;
};
