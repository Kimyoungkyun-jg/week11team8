#include "EnginePCH.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

void UAnimInstance::InitializeGraph()
{
	if (!RootNode)
	{
		return;
	}
	const USkeletalMesh* Mesh = OwningComponent ? OwningComponent->GetSkeletalMesh() : nullptr;
	FAnimInitContext Context;
	Context.Instance = this;
	Context.RefSkeleton = Mesh ? &Mesh->RefSkeleton : nullptr;
	RootNode->Initialize(Context);
}

void UAnimInstance::SetRootNode(TUniquePtr<FAnimNode_Base> InRootNode)
{
	RootNode = std::move(InRootNode);
	InitializeGraph();
}

void UAnimInstance::UpdateAnimation(float DeltaTime)
{
	NativeUpdateAnimation(DeltaTime);
	if (RootNode)
	{
		FAnimUpdateContext Context;
		Context.Instance = this;
		Context.DeltaTime = DeltaTime;
		RootNode->Update(Context);
	}
}

bool UAnimInstance::EvaluateAnimation(TArray<FBoneTransform>& OutPose)
{
	return RootNode && RootNode->Evaluate(OutPose);
}

float UAnimInstance::GetFloat(const FString& Name, float Default) const
{
	const float* Value = Variables.Find(Name);
	return Value ? *Value : Default;
}
