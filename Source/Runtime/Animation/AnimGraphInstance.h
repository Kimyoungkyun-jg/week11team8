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
	int32 AddSequenceState(FAnimNode_StateMachine& Machine, const FString& Name, const FString& AssetPath,
		bool bLoop, FAnimNode_SequencePlayer*& OutPlayer);
};
