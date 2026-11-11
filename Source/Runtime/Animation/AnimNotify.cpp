#include "EnginePCH.h"
#include "Animation/AnimNotify.h"
#include "Asset/AnimSequence.h"
#include "Asset/AssetManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Classes/Sound/SoundWave.h"
#include "Engine/GameplayStatics.h"
#include "Engine/World.h"
#include "Render/SkeletalMesh.h"

void UAnimNotify_Log::Notify(const FAnimNotifyContext& Context)
{
	const FString SequencePath = Context.Sequence ? Context.Sequence->GetPath() : FString("None");
	const FString EventName = Context.Event ? Context.Event->Name : FString("None");
	const float TriggerTime = Context.Event ? Context.Event->TriggerTime : 0.0f;
	HTR_LOG(Info, "[AnimNotify] {} '{}' @ {:.3f}s weight {:.2f} ({}) {}", GetClass()->Name, EventName, TriggerTime, Context.Weight, SequencePath, Message);
}

void UAnimNotify_PlaySound::Notify(const FAnimNotifyContext& Context)
{
	USkeletalMeshComponent* Mesh = Context.MeshComponent;
	if (!Mesh || SoundPath.empty())
	{
		return;
	}

	if (SoundPath == FailedSoundPath)
	{
		return;
	}
	USoundWave* Sound = UAssetManager::Get().LoadSound(SoundPath);
	if (!Sound)
	{
		FailedSoundPath = SoundPath;
		HTR_LOG(Warning, "[AnimNotify] {}: cannot load sound '{}'", GetClass()->Name, SoundPath);
		return;
	}

	// 기본 위치는 컴포넌트 원점. 본이 지정되어 있고 찾으면 그 본의 월드 위치를 쓴다.
	// 본 변환은 컴포넌트 공간 기준이고 행 벡터라 (본 -> 컴포넌트 -> 월드) 순서로 곱한다.
	FVector Location = Mesh->GetWorldLocation();
	if (!BoneName.empty() && Mesh->GetSkeletalMesh())
	{
		const int32 BoneIndex = Mesh->GetSkeletalMesh()->RefSkeleton.FindBoneIndex(BoneName);
		const TArray<FMatrix>& Globals = Mesh->GetGlobalBoneTransforms();
		if (Globals.IsValidIndex(BoneIndex))
		{
			Location = (Globals[BoneIndex] * Mesh->GetWorldMatrix()).TransformPosition(FVector(0.f, 0.f, 0.f));
		}
		else if (WarnedBoneName != BoneName)
		{
			WarnedBoneName = BoneName;
			HTR_LOG(Warning, "[AnimNotify] {}: bone '{}' not found, playing at the component location", GetClass()->Name, BoneName);
		}
	}

	UGameplayStatics::PlaySoundAtLocation(Mesh->GetWorld(), Sound, Location, Volume * Context.Weight, Pitch);
}
