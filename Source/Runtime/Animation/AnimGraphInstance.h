#pragma once

#include "Animation/AnimInstance.h"
#include "AnimGraphInstance.generated.h"

UCLASS()
class UAnimGraphInstance : public UAnimInstance
{
	GENERATED_BODY()
protected:
	void NativeInitializeAnimation() override;
	virtual void BuildGraph() {}
};
