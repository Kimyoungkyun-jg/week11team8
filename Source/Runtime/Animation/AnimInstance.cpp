#include "EnginePCH.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNotify.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Render/SkeletalMesh.h"

UAnimInstance::~UAnimInstance()
{
	for (FAnimNode_Base* Node : OwnedNodes)
	{
		delete Node;
	}
}

USkeletalMeshComponent* UAnimInstance::GetOwningComponent() const
{
	return Cast<USkeletalMeshComponent>(GetOuter());
}

AActor* UAnimInstance::GetOwningActor() const
{
	USkeletalMeshComponent* Component = GetOwningComponent();
	return Component ? Component->GetOwner() : nullptr;
}

APawn* UAnimInstance::TryGetPawnOwner() const
{
	return Cast<APawn>(GetOwningActor());
}

void UAnimInstance::InitializeAnimation()
{
	RootNode = nullptr;
	for (FAnimNode_Base* Node : OwnedNodes)
	{
		delete Node;
	}
	OwnedNodes.Reset();
	NativeInitializeAnimation();
	if (RootNode)
	{
		RootNode->Reset();
	}
}

void UAnimInstance::UpdateAnimation(float DeltaSeconds)
{
	// 호출되지 않고 남은 Notify가 다음 프레임에 섞이지 않도록 먼저 비운다.
	NotifyQueue.Reset();
	//입력 변수 계산
	NativeUpdateAnimation(DeltaSeconds);
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (Mesh && Mesh->Skeleton && RootNode)
	{
		RootNode->Update(DeltaSeconds);
		// 전파: 트리의 Notify를 가중치와 함께 모은다. 호출은 TriggerAnimNotifies에서 한다.
		RootNode->GatherNotifies(1.0f, NotifyQueue);
	}
}

void UAnimInstance::TriggerAnimNotifies()
{
	if (NotifyQueue.IsEmpty())
	{
		return;
	}
	USkeletalMeshComponent* Component = GetOwningComponent();
	if (!Component)
	{
		NotifyQueue.Reset();
		return;
	}
	const UWorld* World = Component->GetWorld();
	const bool bGameWorld = World && World->IsGameWorld();

	// Notify가 호출 중에 애니메이션 상태를 바꾸거나 객체를 파괴해도 순회가 깨지지 않도록 큐를 옮겨 두고 시작한다.
	TArray<FAnimNotifyQueueItem> Queue = std::move(NotifyQueue);
	NotifyQueue.Reset();
	for (const FAnimNotifyQueueItem& Item : Queue)
	{
		if (!IsValid(Component))
		{
			break;
		}
		UAnimNotify* Notify = Item.Event.Notify;
		if (!Notify || Item.Weight < Item.Event.TriggerWeightThreshold)
		{
			continue;
		}
		if (!bGameWorld && !Notify->CanTriggerInEditorWorld())
		{
			continue;
		}
		FAnimNotifyContext Context;
		Context.MeshComponent = Component;
		Context.Sequence = Item.Source;
		Context.Event = &Item.Event;
		Context.Weight = Item.Weight;
		Notify->Notify(Context);
	}
}

bool UAnimInstance::EvaluatePose(TArray<FMatrix>& OutLocalPose)
{
	OutLocalPose.Reset();
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh || !Mesh->Skeleton)
	{
		return false;
	}
	TArray<FAnimBoneTransform> Pose;
	if (!RootNode || !RootNode->Evaluate(Mesh->Skeleton, Pose) || Pose.Num() != Mesh->Skeleton->RefSkeleton.GetNum())
	{
		return false;
	}
	OutLocalPose.SetNum(Pose.Num());
	for (int32 Index = 0; Index < Pose.Num(); ++Index)
	{
		const FAnimBoneTransform& Bone = Pose[Index];
		OutLocalPose[Index] = MakeAnimMatrix(Bone.Position, Bone.Rotation, Bone.Scale);
	}
	return true;
}

bool UAnimInstance::ConsumeRootMotion(FMatrix& OutDelta)
{
	return RootNode && RootNode->ConsumeRootMotion(OutDelta);
}
