#include "EnginePCH.h"
#include "Animation/AnimNode_TwoWayBlend.h"
#include "Animation/BoneTransform.h"
#include <algorithm>
#include <cmath>

void FAnimNode_TwoWayBlend::SetAlpha(float InAlpha)
{
	const float ClampedAlpha = std::isnan(InAlpha) ? 0.0f : std::clamp(InAlpha, 0.0f, 1.0f);
	if (Alpha != ClampedAlpha)
	{
		Alpha = ClampedAlpha;
		bBlendDirty = true;
	}
}

void FAnimNode_TwoWayBlend::Initialize_AnyThread()
{
	A.Initialize();
	B.Initialize();
	CachedPoseA.Reset();
	CachedPoseB.Reset();
	PoseMesh = nullptr;
	bBlendDirty = true;
}

void FAnimNode_TwoWayBlend::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	A.Update(Context);
	B.Update(Context);
}

bool FAnimNode_TwoWayBlend::Evaluate_AnyThread(FPoseContext& Output)
{
	if (PoseMesh != &Output.Mesh)
	{
		CachedPoseA.Reset();
		CachedPoseB.Reset();
		PoseMesh = &Output.Mesh;
		bBlendDirty = true;
	}

	// 새 샘플이 없는 입력은 이전에 평가한 포즈를 다시 쓴다.
	FPoseContext PoseA{ Output.Mesh, CachedPoseA };
	FPoseContext PoseB{ Output.Mesh, CachedPoseB };
	const bool bUpdatedA = A.Evaluate(PoseA);
	const bool bUpdatedB = B.Evaluate(PoseB);
	if ((!bUpdatedA && !bUpdatedB && !bBlendDirty) || (CachedPoseA.IsEmpty() && CachedPoseB.IsEmpty()))
	{
		return false;
	}

	TArray<FMatrix>& OutLocalPose = Output.Pose;
	if (CachedPoseB.IsEmpty() || (Alpha <= 0.0f && !CachedPoseA.IsEmpty()))
	{
		OutLocalPose = CachedPoseA;
	}
	else if (CachedPoseA.IsEmpty() || Alpha >= 1.0f)
	{
		OutLocalPose = CachedPoseB;
	}
	else
	{
		// 두 입력 모두 대상 메시의 본 순서를 사용한다.
		OutLocalPose.SetNum(CachedPoseA.Num());
		for (int32 BoneIndex = 0; BoneIndex < CachedPoseA.Num(); ++BoneIndex)
		{
			const FBoneTransform TransformA = FBoneTransform::FromMatrix(CachedPoseA[BoneIndex]);
			const FBoneTransform TransformB = FBoneTransform::FromMatrix(CachedPoseB[BoneIndex]);
			OutLocalPose[BoneIndex] = FBoneTransform::Blend(TransformA, TransformB, Alpha).ToMatrix();
		}
	}
	bBlendDirty = false;
	return true;
}
