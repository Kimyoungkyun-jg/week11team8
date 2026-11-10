#pragma once

#include "Animation/AnimNodeBase.h"

struct FAnimNode_TwoWayBlend : public FAnimNode_Base
{
	FPoseLink A;
	FPoseLink B;

	void SetAlpha(float InAlpha);
	void Initialize_AnyThread() override;
	void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	bool Evaluate_AnyThread(FPoseContext& Output) override;

private:
	TArray<FMatrix> CachedPoseA;
	TArray<FMatrix> CachedPoseB;
	const USkeletalMesh* PoseMesh = nullptr;
	float Alpha = 0.0f;
	bool bBlendDirty = true;
};
