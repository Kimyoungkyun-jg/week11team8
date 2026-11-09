#include "EnginePCH.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include <algorithm>
#include <cmath>

void UAnimSingleNodeInstance::SetAnimationAsset(UAnimSequence* InSequence)
{
	Sequence = InSequence;
	CurrentTime = 0.0f;
	TrackToBone.Reset();

	// 연결표는 컴포넌트가 실제로 쓰는 메시의 본 테이블 기준이다. SetLocalPose가 이 본 수를 검사한다.
	const USkeletalMeshComponent* Component = GetOwningComponent();
	const USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (Sequence && Mesh)
	{
		Sequence->BuildTrackToBoneMap(Mesh->RefSkeleton, TrackToBone);
	}
	bPlaying = Sequence != nullptr;
}

void UAnimSingleNodeInstance::SetPosition(float InTime)
{
	const float Length = Sequence ? Sequence->GetPlayLength() : 0.0f;
	CurrentTime = std::isfinite(InTime) ? std::min(std::max(InTime, 0.0f), Length) : 0.0f;
}

void UAnimSingleNodeInstance::UpdateAnimation(float DeltaTime)
{
	if (!bPlaying || !Sequence)
	{
		return;
	}

	const float Length = Sequence->GetPlayLength();
	if (Length <= 0.0f)
	{
		// 키가 1개뿐인 시퀀스. 진행할 시간이 없으므로 그 자세를 유지한다.
		CurrentTime = 0.0f;
		bPlaying = bLooping;
		return;
	}

	CurrentTime += DeltaTime * PlayRate;
	if (bLooping)
	{
		// 마지막 키(Length)에 닿으면 0으로 감긴다. PlayRate가 음수여도 [0, Length)로 돌려보낸다.
		// 키 사이만 보간하므로 이어지는 루프가 되려면 마지막 자세가 첫 자세와 같아야 한다.
		CurrentTime = std::fmod(CurrentTime, Length);
		if (CurrentTime < 0.0f)
		{
			CurrentTime += Length;
		}
	}
	else if (CurrentTime >= Length)
	{
		CurrentTime = Length;
		bPlaying = false;
	}
	else if (CurrentTime <= 0.0f)
	{
		CurrentTime = 0.0f;
		bPlaying = false;
	}
}

bool UAnimSingleNodeInstance::EvaluateAnimation(TArray<FBoneTransform>& OutPose)
{
	const USkeletalMeshComponent* Component = GetOwningComponent();
	const USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!Sequence || !Mesh)
	{
		return false;
	}
	Sequence->GetBonePose(CurrentTime, Mesh->RefSkeleton, TrackToBone, OutPose);
	return true;
}
