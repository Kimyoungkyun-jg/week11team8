#include "EnginePCH.h"
#include "GameFramework/TestCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "Animation/TestAnimInstance.h"
#include "Asset/AssetManager.h"

ATestCharacter::ATestCharacter()
{
	USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMeshComponent();
	USkeletalMesh* Mesh = UAssetManager::LoadFbxSkeletalMesh(
		"Assets/Models/UnrealModel/SK_SKM_Quinn_Simple_root.fbxbin");
	if (Mesh)
	{
		SkeletalMeshComponent->SetSkeletalMesh(Mesh);
		if (!SkeletalMeshComponent->SetAnimGraphPath("Assets/Animations/Quinn.animgraph"))
			HTR_LOG(Error, "Failed to assign Quinn.animgraph to ATestCharacter");
	}
	else HTR_LOG(Error, "Failed to load SK_SKM_Quinn_Simple_root.fbxbin for ATestCharacter");
	GetCharacterMovement()->MaxWalkSpeed = 6.0f;
}

void ATestCharacter::Attack()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (bJumpLocked || bAttackLocked || !Movement || !Movement->CanJump()) return;
	if (UTestAnimInstance* Anim = Cast<UTestAnimInstance>(GetSkeletalMeshComponent()->GetAnimInstance());
		Anim && Anim->HasAttackAnimation())
		Anim->RequestAttack();
}

void ATestCharacter::Jump()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!bJumpLocked && !bAttackLocked && Movement && Movement->CanJump()) ACharacter::Jump();
}
