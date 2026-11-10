#pragma once

#include "Animation/AnimNodeBase.h"
#include "Core/Types.h"

class UAnimDataModel;
class UAnimSequence;

struct FAnimNode_SequencePlayer : public FAnimNode_Base
{
	void SetSequence(const UAnimSequence* Sequence, bool bInLoop);
	void Initialize_AnyThread() override;
	void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	bool Evaluate_AnyThread(FPoseContext& Output) override;

private:
	void BindTracks(const USkeletalMesh& Mesh);

	TSharedPtr<const UAnimDataModel> DataModel;
	const USkeletalMesh* BoundMesh = nullptr;
	TArray<int32> TrackToBone;
	TArray<float> CurveValues;
	TArray<FMatrix> CurveLocalPose;
	float CurrentTime = 0.0f;
	float LastEvaluatedTime = -1.0f;
	bool bLoop = false;
};
