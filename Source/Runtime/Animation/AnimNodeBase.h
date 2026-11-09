#pragma once

#include "Asset/AnimTransform.h"
#include "Containers/Array.h"
#include "Core/EngineString.h"

#include <functional>

class UAnimSequence;
class UBlendSpace;
class USkeleton;

struct FAnimNode_Base
{
	virtual ~FAnimNode_Base() = default;
	virtual void Reset() {}
	virtual void Update(float DeltaSeconds) {}
	virtual bool Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose) = 0;
	virtual bool ConsumeRootMotion(FMatrix& OutDelta) { return false; }
};

struct FAnimNode_SequencePlayer : FAnimNode_Base
{
	UAnimSequence* Sequence = nullptr;
	float CurrentTime = 0.0f;
	float PlayRate = 1.0f;
	bool bLoop = true;
	bool bPlaying = true;
	FMatrix PendingRootMotion = FMatrix::Identity;
	bool bHasPendingRootMotion = false;
	bool IsFinished() const;
	float GetRemainingTime() const;

	void Reset() override;
	void Update(float DeltaSeconds) override;
	bool Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose) override;
	bool ConsumeRootMotion(FMatrix& OutDelta) override;
};

struct FAnimNode_TwoWayBlend : FAnimNode_Base
{
	FAnimNode_Base* A = nullptr;
	FAnimNode_Base* B = nullptr;
	float Alpha = 0.0f;

	void Reset() override;
	void Update(float DeltaSeconds) override;
	bool Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose) override;
	bool ConsumeRootMotion(FMatrix& OutDelta) override;
};

struct FAnimNode_BlendSpacePlayer : FAnimNode_Base
{
	UBlendSpace* BlendSpace = nullptr;
	float InputX = 0.0f;
	float InputY = 0.0f;
	float NormalizedTime = 0.0f;
	float PlayRate = 1.0f;
	bool bLoop = true;
	bool bPlaying = true;
	FMatrix PendingRootMotion = FMatrix::Identity;
	bool bHasPendingRootMotion = false;

	void Reset() override;
	void Update(float DeltaSeconds) override;
	bool Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose) override;
	bool ConsumeRootMotion(FMatrix& OutDelta) override;
};

struct FAnimNode_StateMachine : FAnimNode_Base
{
	struct FState
	{
		FString Name;
		FAnimNode_Base* Pose = nullptr;
	};
	struct FTransition
	{
		int32 FromState = -1;
		int32 ToState = -1;
		std::function<bool()> Condition;
		float BlendTime = 0.0f;
		std::function<void()> OnCompleted;
		std::function<void()> OnStarted;
	};

	TArray<FState> States;
	TArray<FTransition> Transitions;
	int32 CurrentState = -1;
	int32 NextState = -1;
	float TransitionElapsed = 0.0f;
	float TransitionDuration = 0.0f;
	std::function<void()> TransitionOnCompleted;

	int32 AddState(const FString& Name, FAnimNode_Base* PoseNode);
	bool AddTransition(int32 FromState, int32 ToState, std::function<bool()> Condition,
		float BlendTime, std::function<void()> OnCompleted = {}, std::function<void()> OnStarted = {});
	void FinishTransition();
	void Reset() override;
	void Update(float DeltaSeconds) override;
	bool Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose) override;
	bool ConsumeRootMotion(FMatrix& OutDelta) override;
};
