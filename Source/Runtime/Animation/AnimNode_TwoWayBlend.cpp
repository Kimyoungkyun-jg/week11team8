#include "EnginePCH.h"
#include "Animation/AnimNode_TwoWayBlend.h"
#include "Animation/AnimInstance.h"
#include <algorithm>
#include <cmath>

void FAnimNode_TwoWayBlend::Initialize(const FAnimInitContext& Context)
{
	RefSkeleton = Context.RefSkeleton;
	if (A) A->Initialize(Context);
	if (B) B->Initialize(Context);
}

void FAnimNode_TwoWayBlend::Update(const FAnimUpdateContext& Context)
{
	if (Context.Instance && !AlphaVariable.empty())
	{
		Alpha = Context.Instance->GetFloat(AlphaVariable, Alpha);
	}
	if (A) A->Update(Context);
	if (B) B->Update(Context);
}

bool FAnimNode_TwoWayBlend::Evaluate(TArray<FBoneTransform>& OutPose)
{
	if (!RefSkeleton)
	{
		return false;
	}
	const TArray<FBoneTransform>& Reference = RefSkeleton->RefBonePose;

	// 한쪽이 포즈를 못 내면 바인드 포즈로 대신해 그대로 섞는다. 한쪽 실패가 전체 실패가 되면 연결을 끊고 이어 붙일 때 포즈가 튄다.
	const bool bHasA = A && A->Evaluate(PoseA) && PoseA.Num() == Reference.Num();
	const bool bHasB = B && B->Evaluate(PoseB) && PoseB.Num() == Reference.Num();
	if (!bHasA && !bHasB)
	{
		return false;
	}
	if (!bHasA) PoseA = Reference;
	if (!bHasB) PoseB = Reference;

	const float Weight = std::isfinite(Alpha) ? std::min(std::max(Alpha, 0.0f), 1.0f) : 0.0f;
	OutPose.SetNum(Reference.Num());
	for (int32 Bone = 0; Bone < Reference.Num(); ++Bone)
	{
		OutPose[Bone] = FBoneTransform::Blend(PoseA[Bone], PoseB[Bone], Weight);
	}
	return true;
}
