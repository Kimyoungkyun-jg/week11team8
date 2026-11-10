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

	// 블렌드 동기화용. 한 주기를 재생하는 데 걸리는 실제 시간(초), 주기 안의 위치(0~1), 반복 여부다.
	// 지원하지 않는 노드는 0/false를 돌려주고 동기화에서 빠진다.
	virtual float GetCycleSeconds() const { return 0.0f; }
	virtual bool GetNormalizedTime(float& OutTime) const { return false; }
	virtual void SetNormalizedTime(float Time) {}
	virtual bool IsLooping() const { return false; }
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
	float GetCycleSeconds() const override;
	bool GetNormalizedTime(float& OutTime) const override;
	void SetNormalizedTime(float Time) override;
	bool IsLooping() const override { return bLoop; }
};

struct FAnimNode_TwoWayBlend : FAnimNode_Base
{
	FAnimNode_Base* A = nullptr;
	FAnimNode_Base* B = nullptr;
	float Alpha = 0.0f;
	// 둘 다 반복 재생이면 가중치가 큰 쪽(리더)의 주기 위치에 다른 쪽을 맞춘다(UE 싱크 그룹과 같은 목적).
	// 걷기·달리기처럼 길이가 다른 루프를 섞어도 발 위치가 어긋나지 않는다.
	bool bSyncByNormalizedTime = true;

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
	float GetCycleSeconds() const override;
	bool GetNormalizedTime(float& OutTime) const override { OutTime = NormalizedTime; return BlendSpace != nullptr; }
	void SetNormalizedTime(float Time) override { NormalizedTime = std::clamp(Time, 0.0f, 1.0f); }
	bool IsLooping() const override { return bLoop; }
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
	TArray<FAnimBoneTransform> InterruptedPose;
	TArray<FAnimBoneTransform> LastEvaluatedPose;

	int32 AddState(const FString& Name, FAnimNode_Base* PoseNode);
	bool AddTransition(int32 FromState, int32 ToState, std::function<bool()> Condition,
		float BlendTime, std::function<void()> OnCompleted = {}, std::function<void()> OnStarted = {});
	void FinishTransition();
	void Reset() override;
	void Update(float DeltaSeconds) override;
	bool Evaluate(const USkeleton* Skeleton, TArray<FAnimBoneTransform>& OutPose) override;
	bool ConsumeRootMotion(FMatrix& OutDelta) override;
};
