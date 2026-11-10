#pragma once

#include "Asset/AnimationAsset.h"
#include "Asset/AnimTransform.h"
#include "BlendSpace.generated.h"

UCLASS()
class UBlendSpace : public UAnimationAsset
{
	GENERATED_BODY()
public:
	virtual float GetEffectiveLength(float X, float Y) const { return 0.0f; }
	virtual bool Evaluate(float X, float Y, float NormalizedTime,
		TArray<FAnimBoneTransform>& OutPose) const { return false; }
	virtual bool ExtractRootMotion(float X, float Y, float PreviousTime, float CurrentTime,
		int32 LoopCount, FMatrix& OutDelta) const { return false; }
};
