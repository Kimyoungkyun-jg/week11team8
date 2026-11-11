#include "EnginePCH.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Render/SkeletalMesh.h"

UAnimInstance::~UAnimInstance()
{
	for (FAnimNode_Base* Node : OwnedNodes)
	{
		delete Node;
	}
}

USkeletalMeshComponent* UAnimInstance::GetOwningComponent() const
{
	return Cast<USkeletalMeshComponent>(GetOuter());
}

AActor* UAnimInstance::GetOwningActor() const
{
	USkeletalMeshComponent* Component = GetOwningComponent();
	return Component ? Component->GetOwner() : nullptr;
}

APawn* UAnimInstance::TryGetPawnOwner() const
{
	return Cast<APawn>(GetOwningActor());
}

void UAnimInstance::InitializeAnimation()
{
	RootNode = nullptr;
	for (FAnimNode_Base* Node : OwnedNodes)
	{
		delete Node;
	}
	OwnedNodes.Reset();
	NativeInitializeAnimation();
	if (RootNode)
	{
		RootNode->Reset();
	}
}

void UAnimInstance::UpdateAnimation(float DeltaSeconds)
{
	//입력 변수 계산
	NativeUpdateAnimation(DeltaSeconds);
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (Mesh && Mesh->Skeleton && RootNode)
	{
		RootNode->Update(DeltaSeconds);
	}
}

bool UAnimInstance::EvaluatePose(TArray<FMatrix>& OutLocalPose)
{
	OutLocalPose.Reset();
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh || !Mesh->Skeleton)
	{
		return false;
	}
	TArray<FAnimBoneTransform> Pose;
	if (!RootNode || !RootNode->Evaluate(Mesh->Skeleton, Pose) || Pose.Num() != Mesh->Skeleton->RefSkeleton.GetNum())
	{
		return false;
	}
	OutLocalPose.SetNum(Pose.Num());
	for (int32 Index = 0; Index < Pose.Num(); ++Index)
	{
		const FAnimBoneTransform& Bone = Pose[Index];
		OutLocalPose[Index] = MakeAnimMatrix(Bone.Position, Bone.Rotation, Bone.Scale);
	}
	return true;
}

bool UAnimInstance::ConsumeRootMotion(FMatrix& OutDelta)
{
	return RootNode && RootNode->ConsumeRootMotion(OutDelta);
}
