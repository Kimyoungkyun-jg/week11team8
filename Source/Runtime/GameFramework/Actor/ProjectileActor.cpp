#include "EnginePCH.h"
#include "GameFramework/Actor/ProjectileActor.h"
#include "GameFramework/Actor/ZombieActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/ProjectileMovementComponent.h"
#include "Asset/AssetManager.h"
#include "Engine/World.h"

AProjectileActor::AProjectileActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>("DefaultSceneRoot"));
    GetRootComponent()->SetCollisionRadiusOverride(0.5f);
    StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("ProjectileMeshComponent");
    // 이동용 루트는 표시 상태와 분리한다. 숨겨진 메쉬를 부모로 두면 FireBall 자식도 함께 숨겨진다.
    StaticMeshComponent->SetupAttachment(GetRootComponent());
    StaticMeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Sphere"));
    StaticMeshComponent->SetRelativeScale3D(FVector(0.5f));
    StaticMeshComponent->SetVisible(false);
    StaticMeshComponent->SetHiddenInGame(true);
    StaticMeshComponent->SetAutoActivate(false);
    FireBallLightComponent = CreateDefaultSubobject<UPointLightComponent>("FireBallLightComponent");
    FireBallLightComponent->SetIntensity(1.8f);
    FireBallLightComponent->SetLightColor(FColor(255, 0, 204, 255));
    FireBallLightComponent->SetAttenuationRadius(6.5f);
    FireBallLightComponent->SetFalloff(3.0f);
    FireBallLightComponent->SetupAttachment(GetRootComponent());
    FireBallLightComponent->SetVisibility(false);
    FireBallLightComponent->SetHiddenInGame(true);
    FireBallLightComponent->SetAutoActivate(false);
    // 라이트는 눈에 보이지 않으므로 발광체 역할의 구체를 함께 움직인다.
    FireBallMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("FireBallMeshComponent");
    FireBallMeshComponent->SetupAttachment(GetRootComponent());
    FireBallMeshComponent->SetStaticMesh(UAssetManager::GetAssetByPath<UStaticMesh>("Sphere"));
    FireBallMeshComponent->SetRelativeScale3D(FVector(FireBallLightComponent->GetAttenuationRadius() * 0.5f));
    FireBallMeshComponent->SetVisible(false);
    FireBallMeshComponent->SetHiddenInGame(true);
    FireBallMeshComponent->SetAutoActivate(false);
    ProjectileMovementComponent = CreateDefaultSubobject<UProjectileMovementComponent>("ProjectileMovementComponent");
    ProjectileMovementComponent->SetAutoActivate(false);
    ProjectileMovementComponent->SetUpdatedComponent(GetRootComponent());
    ProjectileMovementComponent->InintialSpeed = 20.0f;
    ProjectileMovementComponent->MaxSpeed = 100.0f;
    ProjectileMovementComponent->ProjectileGravityScale = 1.0f;
    ProjectileMovementComponent->bShouldBounce = true;
    ProjectileMovementComponent->bRotationFollowsVelocity = true;
    ProjectileMovementComponent->bInitialVelocityInLocalSpace = false;
    ProjectileMovementComponent->bSimulationEnabled = false;
    ProjectileMovementComponent->PrimaryComponentTick.bStartWithTickEnabled = false;
}

void AProjectileActor::BeginPlay()
{
    Super::BeginPlay();
    ProjectileMovementComponent->OnProjectileImpact.AddDynamic(this, &AProjectileActor::HandleProjectileImpact);
    if (bInFlight)
    {
        ProjectileMovementComponent->Activate(true);
        PrimaryActorTick.SetTickFunctionEnable(true);
    }
}

void AProjectileActor::Launch(const FVector& Position, const FVector& Direction, bool bUseStaticMesh)
{
    if (!GetWorld() || !GetWorld()->IsGameWorld() || Direction.Dot(Direction) <= 0.0001f)
        return;
    ReturnToPool();
    const FVector Forward = Direction.Normalized();
    GetRootComponent()->SetWorldLocation(Position, false);
    if (bUseStaticMesh)
    {
        StaticMeshComponent->SetVisible(true);
        StaticMeshComponent->Activate();
        StaticMeshComponent->SetHiddenInGame(false);
    }
    else
    {
        FireBallMeshComponent->SetRelativeScale3D(FVector(FireBallLightComponent->GetAttenuationRadius() * 0.5f));
        FireBallMeshComponent->SetVisible(true);
        FireBallMeshComponent->Activate();
        FireBallMeshComponent->SetHiddenInGame(false);
        FireBallLightComponent->SetVisibility(true);
        FireBallLightComponent->Activate();
        FireBallLightComponent->SetHiddenInGame(false);
    }
    ProjectileMovementComponent->SetUpdatedComponent(GetRootComponent());
    ProjectileMovementComponent->Velocity = Forward * ProjectileMovementComponent->InintialSpeed;
    ProjectileMovementComponent->bSimulationEnabled = true;
    ProjectileMovementComponent->Activate(true);
    RemainingLife = LifeTime;
    bInFlight = true;
    PrimaryActorTick.SetTickFunctionEnable(true);
}

void AProjectileActor::ReturnToPool()
{
    bInFlight = false;
    RemainingLife = 0.0f;
    if (ProjectileMovementComponent)
    {
        ProjectileMovementComponent->Velocity = FVector::ZeroVector;
        ProjectileMovementComponent->bSimulationEnabled = false;
        ProjectileMovementComponent->Deactivate();
    }
    if (StaticMeshComponent)
    {
        StaticMeshComponent->Deactivate();
        StaticMeshComponent->SetHiddenInGame(true);
        StaticMeshComponent->SetVisible(false);
    }
    if (FireBallLightComponent)
    {
        FireBallLightComponent->Deactivate();
        FireBallLightComponent->SetHiddenInGame(true);
        FireBallLightComponent->SetVisibility(false);
    }
    if (FireBallMeshComponent)
    {
        FireBallMeshComponent->Deactivate();
        FireBallMeshComponent->SetHiddenInGame(true);
        FireBallMeshComponent->SetVisible(false);
    }
    PrimaryActorTick.SetTickFunctionEnable(false);
}

void AProjectileActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (!bInFlight) return;
    RemainingLife -= DeltaTime;
    if (RemainingLife <= 0.0f || !ProjectileMovementComponent->bSimulationEnabled) ReturnToPool();
}

void AProjectileActor::HandleProjectileImpact(AActor* OtherActor)
{
    if (!bInFlight) return;
    if (AZombieActor* Zombie = Cast<AZombieActor>(OtherActor))
    {
        Zombie->ReactToHit();
        ReturnToPool();
    }
}
