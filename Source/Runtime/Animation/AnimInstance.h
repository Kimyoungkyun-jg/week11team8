#pragma once

#include "Containers/Array.h"
#include "Animation/AnimNodeBase.h"
#include "Math/Matrix.h"
#include "UObject/Object.h"
#include <utility>
#include "AnimInstance.generated.h"

class AActor;
class APawn;
class USkeletalMeshComponent;

UCLASS()
class UAnimInstance : public UObject
{
	GENERATED_BODY()
public:
	virtual ~UAnimInstance() override;
	USkeletalMeshComponent* GetOwningComponent() const;
	AActor* GetOwningActor() const;
	virtual APawn* TryGetPawnOwner() const;

	void InitializeAnimation();
	void UpdateAnimation(float DeltaSeconds);
	virtual bool EvaluatePose(TArray<FMatrix>& OutLocalPose);
	bool ConsumeRootMotion(FMatrix& OutDelta);
	// UpdateAnimation에서 노드 트리로부터 모은 Notify를 호출한다. 포즈와 본 행렬 갱신이 끝난 뒤(TickComponent 끝)에 불러야 한다.
	void TriggerAnimNotifies();

protected:
	template <typename T, typename... Args>
	T* CreateNode(Args&&... Arguments)
	{
		T* Node = new T(std::forward<Args>(Arguments)...);
		OwnedNodes.Add(Node);
		return Node;
	}
	FAnimNode_Base* RootNode = nullptr;
	virtual void NativeInitializeAnimation() {}
	virtual void NativeUpdateAnimation(float DeltaSeconds) {}
private:
	TArray<FAnimNode_Base*> OwnedNodes;
	// 이번 프레임의 UpdateAnimation에서 모은 Notify. TriggerAnimNotifies가 소비한다.
	TArray<FAnimNotifyQueueItem> NotifyQueue;
};
