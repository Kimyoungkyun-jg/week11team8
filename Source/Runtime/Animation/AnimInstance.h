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
};
