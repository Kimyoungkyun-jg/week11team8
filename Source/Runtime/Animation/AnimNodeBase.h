#pragma once

#include "Containers/Array.h"
#include "Math/Matrix.h"

class USkeletalMesh;

struct FAnimationUpdateContext
{
	float DeltaSeconds = 0.0f;
};

struct FPoseContext
{
	const USkeletalMesh& Mesh;
	TArray<FMatrix>& Pose;
};

struct FAnimNode_Base
{
	virtual ~FAnimNode_Base() = default;
	virtual void Initialize_AnyThread() {}
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) = 0;
	// 새 로컬 포즈를 썼을 때만 true를 반환한다.
	virtual bool Evaluate_AnyThread(FPoseContext& Output) = 0;
};

struct FPoseLink
{
	void SetLinkNode(FAnimNode_Base* InNode) { LinkedNode = InNode; }

	void Initialize() const
	{
		if (LinkedNode)
		{
			LinkedNode->Initialize_AnyThread();
		}
	}

	void Update(const FAnimationUpdateContext& Context) const
	{
		if (LinkedNode)
		{
			LinkedNode->Update_AnyThread(Context);
		}
	}

	bool Evaluate(FPoseContext& Output) const
	{
		return LinkedNode && LinkedNode->Evaluate_AnyThread(Output);
	}

private:
	// 인스턴스가 소유하는 노드를 참조한다. 재생 트리에서 노드마다 출력 소비자는 하나다.
	FAnimNode_Base* LinkedNode = nullptr;
};
