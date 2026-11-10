#pragma once

#include "Animation/AnimInstance.h"
#include "Asset/AnimGraphAsset.h"
#include <vector>
#include "AnimGraphInstance.generated.h"

UCLASS()
class UAnimGraphInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	int32 GetActiveStateId() const;
	int32 GetTargetStateId() const;
	float GetSpeed() const { return Speed; }
	float GetDirection() const { return Direction; }
	void RequestJump() { bManualJumpRequested = true; }
	bool SetPreviewGraph(const FAnimGraphAsset& InGraph);
	void SetPreviewFalling(bool bInFalling) { bPreviewMovementOverride = true; bPreviewFalling = bInFalling; }
protected:
	void NativeInitializeAnimation() override;
	void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void BuildGraph();
	int32 AddSequenceState(FAnimNode_StateMachine& Machine, const FString& Name, const FString& AssetPath,
		bool bLoop, FAnimNode_SequencePlayer*& OutPlayer);
private:
	struct FInputBinding
	{
		FAnimNode_BlendSpacePlayer* Player = nullptr;
		EAnimGraphInput X = EAnimGraphInput::Constant;
		EAnimGraphInput Y = EAnimGraphInput::Constant;
		float ConstantX = 0.0f;
		float ConstantY = 0.0f;
	};
	FAnimGraphAsset Graph;
	bool bUsePreviewGraph = false;
	std::vector<FInputBinding> Bindings;
	std::vector<int32> StateIds;
	std::vector<std::pair<int32, FAnimNode_SequencePlayer*>> SequencePlayers;
	FAnimNode_StateMachine* GraphMachine = nullptr;
	float Speed = 0.0f;
	float Direction = 0.0f;
	uint32 LastJumpCounter = 0;
	bool bJumpRequested = false;
	bool bManualJumpRequested = false;
	bool bFalling = false;
	bool bGrounded = false;
	bool bMovingOnGround = false;
	bool bPreviewMovementOverride = false;
	bool bPreviewFalling = false;
	float InputValue(EAnimGraphInput Input, float Constant = 0.0f) const;
};
