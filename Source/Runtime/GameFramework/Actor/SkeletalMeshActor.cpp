#include "EnginePCH.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "Asset/AssetManager.h"

ASkeletalMeshActor::ASkeletalMeshActor()
{
    SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	USkeletalMesh* Mesh = UAssetManager::LoadFbxSkeletalMesh("Assets/Models/FBX/Mint/Mint.fbx");
	SkeletalMeshComponent->SetSkeletalMesh(Mesh);
    SetRootComponent(SkeletalMeshComponent);
}
