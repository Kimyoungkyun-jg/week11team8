#pragma once

#include "UObject/Object.h"
#include "Containers/Array.h"
#include "Animation/BoneTransform.h"
#include "AnimInstance.generated.h"

class USkeletalMeshComponent;

// 컴포넌트마다 하나씩 붙어 매 틱 포즈를 계산하는 객체 (UE UAnimInstance).
// 에셋(UAnimSequence)은 여러 컴포넌트가 함께 쓰므로 재생 시간 같은 상태는 에셋이 아니라 여기에 둔다.
// 컴포넌트는 UpdateAnimation으로 시간을 진행시키고 EvaluateAnimation으로 로컬 포즈를 받아 SetLocalPose에 넘긴다.
// 이 객체는 프로퍼티가 아니라서 씬 저장·PIE 복제에 따라가지 않는다. 복사본은 컴포넌트가 다시 만든다.
UCLASS()
class UAnimInstance : public UObject
{
	GENERATED_BODY()
public:
	void Initialize(USkeletalMeshComponent* InOwningComponent) { OwningComponent = InOwningComponent; }
	USkeletalMeshComponent* GetOwningComponent() const { return OwningComponent; }

	// 시간을 DeltaTime(초)만큼 진행한다.
	virtual void UpdateAnimation(float DeltaTime) { (void)DeltaTime; }

	// 지금 시간의 부모 기준 로컬 포즈를 OutPose에 채운다. 본 수는 대상 메시와 같다.
	// 계산할 수 없으면(시퀀스 없음 등) false를 돌려주고 OutPose는 건드리지 않는다.
	virtual bool EvaluateAnimation(TArray<FBoneTransform>& OutPose) { (void)OutPose; return false; }

private:
	USkeletalMeshComponent* OwningComponent = nullptr;
};
