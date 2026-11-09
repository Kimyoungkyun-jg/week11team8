#pragma once

#include "Animation/AnimGraphInstance.h"
#include "TestAnimInstance.generated.h"

class USimpleBlendSpace2D;

UCLASS()
class UTestAnimInstance : public UAnimGraphInstance
{
	GENERATED_BODY()
public:
	~UTestAnimInstance() override;
	void RequestAttack() { bAttackRequested = true; }
	bool HasAttackAnimation() const { return AttackPlayer != nullptr; }

protected:
	void BuildGraph() override;
	void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	USimpleBlendSpace2D* SimpleBlendSpace = nullptr;
	FAnimNode_BlendSpacePlayer* LocomotionPlayer = nullptr;
	FAnimNode_SequencePlayer* JumpPlayer = nullptr;
	FAnimNode_SequencePlayer* FallLoopPlayer = nullptr;
	FAnimNode_SequencePlayer* LandPlayer = nullptr;
	FAnimNode_SequencePlayer* AttackPlayer = nullptr;
	FAnimNode_StateMachine* StateMachine = nullptr;
	uint32 LastJumpCounter = 0;
	bool bJumpEventThisTick = false;
	bool bAttackRequested = false;
	bool bAttackEventThisTick = false;
	bool bGrounded = false;
	bool bFalling = false;
	bool bMovingOnGround = false;
};
