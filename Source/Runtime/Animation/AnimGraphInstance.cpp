#include "EnginePCH.h"
#include "Animation/AnimGraphInstance.h"
#include "Asset/AnimSequence.h"
#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Render/SkeletalMesh.h"

void UAnimGraphInstance::NativeInitializeAnimation()
{
	BuildGraph();
}

int32 UAnimGraphInstance::AddSequenceState(FAnimNode_StateMachine& Machine, const FString& Name,
	const FString& AssetPath, bool bLoop, FAnimNode_SequencePlayer*& OutPlayer)
{
	OutPlayer = nullptr;
	USkeletalMeshComponent* Component = GetOwningComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Mesh || !Mesh->Skeleton) return -1;
	UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(AssetPath);
	if (!Sequence || Sequence->Skeleton != Mesh->Skeleton || !Sequence->GetDataModel()) return -1;
	OutPlayer = CreateNode<FAnimNode_SequencePlayer>();
	OutPlayer->Sequence = Sequence;
	OutPlayer->bLoop = bLoop;
	return Machine.AddState(Name, OutPlayer);
}
