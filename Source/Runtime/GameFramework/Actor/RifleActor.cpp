#include "EnginePCH.h"
#include "GameFramework/Actor/RifleActor.h"
#include "GameFramework/Actor/ProjectileActor.h"
#include "Components/ProjectileMovementComponent.h"
#include "Asset/AssetManager.h"
#include "Engine/World.h"

ARifleActor::ARifleActor()
{
	SetActorEnableCollision(false);
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("RifleMeshComponent");
	SetRootComponent(StaticMeshComponent);
	UStaticMesh* RifleMesh = UAssetManager::GetAssetByPath<UStaticMesh>(
		"Assets/Models/UnrealModel/SM_SM_Rifle_SM_Rifle.fbxbin");
	if (!RifleMesh) RifleMesh = UAssetManager::GetAssetByPath<UStaticMesh>(
		"Assets/Models/UnrealModel/SM_SM_Rifle.fbxbin");
	StaticMeshComponent->SetStaticMesh(RifleMesh);
}

AProjectileActor* ARifleActor::Fire()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || MaxProjectiles <= 0 || ProjectileSpeed <= 0.0f || IsActorBeingDestroyed()) return nullptr;

	AProjectileActor* Projectile = nullptr;
	for (AProjectileActor* Candidate : Projectiles)
	{
		if (IsValid(Candidate) && !Candidate->IsActorBeingDestroyed() && !Candidate->IsInFlight())
		{
			Projectile = Candidate;
			break;
		}
	}
	if (!Projectile && Projectiles.Num() < static_cast<uint32>(MaxProjectiles))
	{
		Projectile = Cast<AProjectileActor>(World->SpawnActor(AProjectileActor::StaticClass()));
		if (!Projectile) return nullptr;
		Projectile->GetRootComponent()->SetCollisionRadiusOverride(0.035f);
		Projectile->GetStaticMeshComponent()->SetRelativeScale3D(FVector(0.07f, 0.02f, 0.02f));
		Projectiles.Add(Projectile);
	}
	if (!Projectile) return nullptr;

	UProjectileMovementComponent* Movement = Projectile->GetProjectileMovementComponent();
	Movement->InintialSpeed = ProjectileSpeed;
	Movement->MaxSpeed = ProjectileSpeed;
	Movement->ProjectileGravityScale = 0.0f;
	Movement->bShouldBounce = false;
	const FVector Forward = GetActorQuat().GetForwardVector();
	Projectile->Launch(GetActorLocation() + Forward * MuzzleDistance, Forward, true);
	return Projectile;
}

void ARifleActor::EndPlay()
{
	for (AProjectileActor* Projectile : Projectiles)
	{
		if (IsValid(Projectile) && !Projectile->IsActorBeingDestroyed()) Projectile->Destroy();
	}
	Projectiles.Reset();
	Super::EndPlay();
}
