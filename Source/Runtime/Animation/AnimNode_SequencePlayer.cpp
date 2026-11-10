#include "EnginePCH.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "Animation/BoneTransform.h"
#include "Containers/Map.h"
#include "Render/SkeletalMesh.h"
#include <algorithm>
#include <cmath>

void FAnimNode_SequencePlayer::SetSequence(const UAnimSequence* Sequence, bool bInLoop)
{
	DataModel = Sequence ? Sequence->GetSharedDataModel() : nullptr;
	CurrentTime = 0.0f;
	bLoop = bInLoop;
	Initialize_AnyThread();
}

void FAnimNode_SequencePlayer::Initialize_AnyThread()
{
	BoundMesh = nullptr;
	LastEvaluatedTime = -1.0f;
}

void FAnimNode_SequencePlayer::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	if (!DataModel || DataModel->PlayLength <= 0.0f)
	{
		return;
	}

	const float NextTime = CurrentTime + std::max(0.0f, Context.DeltaSeconds);
	CurrentTime = bLoop ? std::fmod(NextTime, DataModel->PlayLength)
		: std::min(NextTime, DataModel->PlayLength);
}

void FAnimNode_SequencePlayer::BindTracks(const USkeletalMesh& Mesh)
{
	const TArray<FBoneInfo>& Bones = Mesh.MeshData.Bones;
	TMap<FName, int32, FNameHash> BoneIndices;
	for (int32 BoneIndex = 0; BoneIndex < Bones.Num(); ++BoneIndex)
	{
		BoneIndices.Add(FName(Bones[BoneIndex].Name), BoneIndex);
	}

	const TArray<FBoneAnimationTrack>& Tracks = DataModel->BoneAnimationTracks;
	TrackToBone.SetNum(Tracks.Num());
	for (int32 TrackIndex = 0; TrackIndex < Tracks.Num(); ++TrackIndex)
	{
		const int32* BoneIndex = BoneIndices.Find(Tracks[TrackIndex].Name);
		TrackToBone[TrackIndex] = BoneIndex ? *BoneIndex : INDEX_NONE;
	}
	BoundMesh = &Mesh;
	LastEvaluatedTime = -1.0f;
}

bool FAnimNode_SequencePlayer::Evaluate_AnyThread(FPoseContext& Output)
{
	if (!DataModel)
	{
		return false;
	}
	if (BoundMesh != &Output.Mesh)
	{
		BindTracks(Output.Mesh);
	}

	const TArray<FBoneInfo>& Bones = Output.Mesh.MeshData.Bones;
	if (Bones.IsEmpty() || CurrentTime == LastEvaluatedTime)
	{
		return false;
	}

	bool bUseCurvePose = false;
	if (DataModel->ComposeCurvePose)
	{
		const TArray<FAnimationCurveData>& Curves = DataModel->CurveData;
		CurveValues.SetNum(Curves.Num());
		for (int32 Index = 0; Index < Curves.Num(); ++Index)
		{
			CurveValues[Index] = Curves[Index].Evaluate(CurrentTime);
		}
		bUseCurvePose = DataModel->ComposeCurvePose(CurrentTime, CurveValues, CurveLocalPose)
			&& CurveLocalPose.Num() == TrackToBone.Num();
	}
	if (!bUseCurvePose && DataModel->NumberOfKeys <= 0)
	{
		return false;
	}

	int32 KeyIndex = 0;
	if (!bUseCurvePose && DataModel->PlayLength > 0.0f)
	{
		const int32 LastKey = DataModel->NumberOfKeys - 1;
		// 마지막 키는 정규 샘플 간격보다 앞선 실제 종료 시각에 저장될 수 있다.
		KeyIndex = CurrentTime >= DataModel->PlayLength ? LastKey
			: std::clamp(static_cast<int32>(CurrentTime * DataModel->FrameRate), 0, LastKey);
	}

	TArray<FMatrix>& OutLocalPose = Output.Pose;
	OutLocalPose.SetNum(Bones.Num());
	for (int32 BoneIndex = 0; BoneIndex < Bones.Num(); ++BoneIndex)
	{
		OutLocalPose[BoneIndex] = Bones[BoneIndex].LocalBindPose;
	}
	for (int32 TrackIndex = 0; TrackIndex < TrackToBone.Num(); ++TrackIndex)
	{
		const int32 BoneIndex = TrackToBone[TrackIndex];
		if (BoneIndex == INDEX_NONE)
		{
			continue;
		}
		if (bUseCurvePose)
		{
			OutLocalPose[BoneIndex] = CurveLocalPose[TrackIndex];
			continue;
		}

		const FRawAnimSequenceTrack& Track = DataModel->BoneAnimationTracks[TrackIndex].InternalTrack;
		if (Track.PosKeys.IsValidIndex(KeyIndex)
			&& Track.RotKeys.IsValidIndex(KeyIndex) && Track.ScaleKeys.IsValidIndex(KeyIndex))
		{
			OutLocalPose[BoneIndex] = FBoneTransform(
				Track.PosKeys[KeyIndex], Track.RotKeys[KeyIndex], Track.ScaleKeys[KeyIndex]).ToMatrix();
		}
	}
	LastEvaluatedTime = CurrentTime;
	return true;
}
