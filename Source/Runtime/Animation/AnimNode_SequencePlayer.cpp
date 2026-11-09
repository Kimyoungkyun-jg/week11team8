#include "EnginePCH.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include <algorithm>
#include <cmath>

void FAnimNode_SequencePlayer::SetSequence(UAnimSequence* InSequence)
{
	Sequence = InSequence;
	CurrentTime = 0.0f;
	bPlaying = Sequence != nullptr;
	RebuildTrackMap();
}

void FAnimNode_SequencePlayer::RebuildTrackMap()
{
	TrackToBone.Reset();
	if (Sequence && RefSkeleton)
	{
		Sequence->BuildTrackToBoneMap(*RefSkeleton, TrackToBone);
	}
}

void FAnimNode_SequencePlayer::Initialize(const FAnimInitContext& Context)
{
	// 메시가 바뀌면 본 테이블이 달라지므로 연결표만 다시 만든다. 재생 시간은 유지한다.
	RefSkeleton = Context.RefSkeleton;
	RebuildTrackMap();
}

void FAnimNode_SequencePlayer::SetPosition(float InTime)
{
	const float Length = Sequence ? Sequence->GetPlayLength() : 0.0f;
	CurrentTime = std::isfinite(InTime) ? std::min(std::max(InTime, 0.0f), Length) : 0.0f;
}

void FAnimNode_SequencePlayer::Update(const FAnimUpdateContext& Context)
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

	CurrentTime += Context.DeltaTime * PlayRate;
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

bool FAnimNode_SequencePlayer::Evaluate(TArray<FBoneTransform>& OutPose)
{
	if (!Sequence || !RefSkeleton)
	{
		return false;
	}
	Sequence->GetBonePose(CurrentTime, *RefSkeleton, TrackToBone, OutPose);
	return true;
}
