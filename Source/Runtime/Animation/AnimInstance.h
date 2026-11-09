#pragma once

#include "UObject/Object.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Core/Types.h"
#include "Animation/BoneTransform.h"
#include "Animation/AnimNode.h"
#include "AnimInstance.generated.h"

class USkeletalMeshComponent;

// 컴포넌트마다 하나씩 붙어 매 틱 포즈를 계산하는 객체 (UE UAnimInstance).
// 인스턴스 자신은 포즈를 만들지 않는다. 루트 노드(FAnimNode_Base)에서 시작하는 노드 트리가 포즈를 만들고,
// 인스턴스는 그 트리의 소유자이자 구동자다: 시간을 진행시키고(UpdateAnimation) 최종 로컬 포즈를 받아 컴포넌트에 넘긴다(EvaluateAnimation).
// 에셋(UAnimSequence)은 여러 컴포넌트가 함께 쓰므로 재생 시간 같은 상태는 에셋이 아니라 노드에 둔다.
// 이 객체는 프로퍼티가 아니라서 씬 저장·PIE 복제에 따라가지 않는다. 복사본은 컴포넌트가 다시 만든다.
UCLASS()
class UAnimInstance : public UObject
{
	GENERATED_BODY()
public:
	void Initialize(USkeletalMeshComponent* InOwningComponent) { OwningComponent = InOwningComponent; }
	USkeletalMeshComponent* GetOwningComponent() const { return OwningComponent; }

	// 소유 컴포넌트의 현재 메시 기준으로 노드 트리를 (다시) 초기화한다. 메시가 정해지거나 바뀔 때 부른다.
	void InitializeGraph();

	// 시간을 DeltaTime(초)만큼 진행한다. NativeUpdateAnimation 뒤에 노드 트리를 Update한다.
	void UpdateAnimation(float DeltaTime);

	// 지금 시간의 부모 기준 로컬 포즈를 OutPose에 채운다. 본 수는 대상 메시와 같다.
	// 계산할 수 없으면(루트 없음, 시퀀스 없음 등) false를 돌려주고 OutPose는 건드리지 않는다.
	bool EvaluateAnimation(TArray<FBoneTransform>& OutPose);

	// 이름으로 찾는 변수 저장소. 게임플레이(C++·Lua)가 값을 쓰고, 노드(블렌드 알파, 이후 State Machine 전환 조건)가 읽는다.
	// 값은 float 하나로 통일한다. bool은 0/1로 저장한다. 인스턴스가 다시 만들어지면 비워진다.
	void SetFloat(const FString& Name, float Value) { Variables.Add(Name, Value); }
	// 없는 이름이면 Default를 돌려준다.
	float GetFloat(const FString& Name, float Default = 0.0f) const;
	bool HasVariable(const FString& Name) const { return Variables.Contains(Name); }
	void SetBool(const FString& Name, bool bValue) { SetFloat(Name, bValue ? 1.0f : 0.0f); }
	bool GetBool(const FString& Name, bool bDefault = false) const { return GetFloat(Name, bDefault ? 1.0f : 0.0f) != 0.0f; }

protected:
	// 노드 트리 갱신 전에 불린다. 게임플레이가 정한 값을 노드에 옮기는 자리다(UE NativeUpdateAnimation).
	virtual void NativeUpdateAnimation(float DeltaTime) { (void)DeltaTime; }

	// 루트 노드를 정한다. 이미 메시가 정해졌다면 바로 초기화한다.
	void SetRootNode(TUniquePtr<FAnimNode_Base> InRootNode);
	FAnimNode_Base* GetRootNode() const { return RootNode.get(); }

private:
	USkeletalMeshComponent* OwningComponent = nullptr;
	TUniquePtr<FAnimNode_Base> RootNode;
	TMap<FString, float> Variables;
};
