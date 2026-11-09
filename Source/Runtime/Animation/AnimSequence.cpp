#include "EnginePCH.h"
#include "Animation/AnimSequence.h"
#include "Animation/ReferenceSkeleton.h"
#include <cmath>
#include <unordered_set>

namespace
{
	// Time이 놓인 키 구간. Key0과 Key1 사이 Alpha 지점이다. 마지막 키에서는 Key0 == Key1이다.
	struct FKeyPosition
	{
		int32 Key0 = 0;
		int32 Key1 = 0;
		float Alpha = 0.0f;
	};

	// 시간 → 키 구간. 키 i는 i / FrameRate초에 있으므로 Time × FrameRate의 정수부가 앞 키, 소수부가 Alpha다.
	FKeyPosition FindKeyPosition(float Time, float FrameRate, int32 NumberOfKeys, float PlayLength)
	{
		FKeyPosition Result;
		if (NumberOfKeys <= 1 || FrameRate <= 0.0f || !std::isfinite(Time))
		{
			return Result;
		}

		// 범위 밖 시간은 양 끝 자세로 자른다. 반복 재생은 재생하는 쪽이 시간을 되감아 처리한다.
		const float Clamped = Time < 0.0f ? 0.0f : (Time > PlayLength ? PlayLength : Time);
		const float KeyPos = Clamped * FrameRate;
		const int32 LastKey = NumberOfKeys - 1;

		Result.Key0 = static_cast<int32>(std::floor(KeyPos));
		if (Result.Key0 >= LastKey)
		{
			// 끝 시간이거나 부동소수 오차로 넘친 경우. 마지막 키 그대로
			Result.Key0 = LastKey;
			Result.Key1 = LastKey;
			Result.Alpha = 0.0f;
			return Result;
		}
		Result.Key1 = Result.Key0 + 1;
		Result.Alpha = KeyPos - static_cast<float>(Result.Key0);
		return Result;
	}

	// 성분 하나 샘플링. 키 수가 1개면 상수 트랙이라 그 값, 0개면 false를 돌려 호출한 쪽이 기본값을 쓰게 한다.
	bool SampleVector(const TArray<FVector>& Keys, const FKeyPosition& Position, FVector& Out)
	{
		if (Keys.IsEmpty()) return false;
		if (Keys.Num() == 1) { Out = Keys[0]; return true; }
		const FVector& A = Keys[Position.Key0];
		const FVector& B = Keys[Position.Key1];
		Out = A + (B - A) * Position.Alpha;
		return true;
	}

	bool SampleQuat(const TArray<FQuat>& Keys, const FKeyPosition& Position, FQuat& Out)
	{
		if (Keys.IsEmpty()) return false;
		if (Keys.Num() == 1) { Out = Keys[0]; return true; }
		// 최단 경로 Slerp. 두 키가 반대 부호(q와 -q)여도 가까운 쪽으로 돈다.
		Out = FQuat::Slerp(Keys[Position.Key0], Keys[Position.Key1], Position.Alpha);
		return true;
	}

	FBoneTransform SampleTrack(const FRawAnimSequenceTrack& Track, const FKeyPosition& Position, const FBoneTransform& Fallback)
	{
		FBoneTransform Result = Fallback;
		SampleVector(Track.PosKeys, Position, Result.Translation);
		SampleQuat(Track.RotKeys, Position, Result.Rotation);
		SampleVector(Track.ScaleKeys, Position, Result.Scale);
		return Result;
	}

	// 성분 하나의 키 수는 시퀀스의 키 수이거나 상수 트랙이면 1개여야 한다.
	bool IsValidKeyCount(int32 Count, int32 NumberOfKeys)
	{
		return Count == 1 || Count == NumberOfKeys;
	}

	bool IsFinite(const FVector& Value)
	{
		return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
	}

	bool IsFinite(const FQuat& Value)
	{
		return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z) && std::isfinite(Value.W);
	}
}

float UAnimSequence::GetPlayLength() const
{
	if (NumberOfKeys <= 1 || FrameRate <= 0.0f)
	{
		return 0.0f;
	}

	//0초부터 시작하는 30프레임짜리면 NumberofKeys가 31개
	//(31 - 1) / 30 = 1초
	return static_cast<float>(NumberOfKeys - 1) / FrameRate;
}

int32 UAnimSequence::FindTrackIndex(const FName& BoneName) const
{
	for (int32 Index = 0; Index < Tracks.Num(); ++Index)
	{
		if (Tracks[Index].BoneName == BoneName)
		{
			return Index;
		}
	}
	return -1;
}

void UAnimSequence::BuildTrackToBoneMap(const FReferenceSkeleton& Target, TArray<int32>& OutTrackToBone) const
{
	// 대상 본 이름 → 번호. 대상 쪽 이름은 FString이라 여기서 한 번 FName으로 바꾼다.
	TMap<FName, int32, FNameHash> BoneByName;
	BoneByName.Empty(Target.RefBoneInfo.Num());
	for (int32 BoneIndex = 0; BoneIndex < Target.RefBoneInfo.Num(); ++BoneIndex)
	{
		const FName Name(Target.RefBoneInfo[BoneIndex].Name);
		// 같은 이름이 여러 번 나오면 앞의 본을 쓴다(FindBoneIndex와 같은 결과).
		if (!BoneByName.Find(Name))
		{
			BoneByName.Add(Name, BoneIndex);
		}
	}

	OutTrackToBone.SetNum(Tracks.Num());
	for (int32 TrackIndex = 0; TrackIndex < Tracks.Num(); ++TrackIndex)
	{
		const int32* BoneIndex = BoneByName.Find(Tracks[TrackIndex].BoneName);
		OutTrackToBone[TrackIndex] = BoneIndex ? *BoneIndex : -1;
	}
}

FBoneTransform UAnimSequence::GetBoneTransform(int32 TrackIndex, float Time, const FBoneTransform& Fallback) const
{
	if (TrackIndex < 0 || TrackIndex >= Tracks.Num())
	{
		return Fallback;
	}
	const FKeyPosition Position = FindKeyPosition(Time, FrameRate, NumberOfKeys, GetPlayLength());
	return SampleTrack(Tracks[TrackIndex].InternalTrack, Position, Fallback);
}

void UAnimSequence::GetBonePose(float Time, const FReferenceSkeleton& Target, const TArray<int32>& TrackToBone, TArray<FBoneTransform>& OutPose) const
{
	// 트랙이 없는 본은 기준 포즈 그대로다.
	OutPose = Target.RefBonePose;
	if (TrackToBone.Num() != Tracks.Num())
	{
		return;
	}

	// 모든 트랙이 같은 시간 격자를 쓰므로 키 구간은 한 번만 구한다.
	const FKeyPosition Position = FindKeyPosition(Time, FrameRate, NumberOfKeys, GetPlayLength());
	for (int32 TrackIndex = 0; TrackIndex < Tracks.Num(); ++TrackIndex)
	{
		const int32 BoneIndex = TrackToBone[TrackIndex];
		if (BoneIndex < 0 || BoneIndex >= OutPose.Num())
		{
			continue;
		}
		// 키가 없는 성분은 그 본의 기준 포즈 값을 유지한다.
		OutPose[BoneIndex] = SampleTrack(Tracks[TrackIndex].InternalTrack, Position, OutPose[BoneIndex]);
	}
}

bool UAnimSequence::Validate(FString& OutError) const
{
	return ValidateKeys(FrameRate, NumberOfKeys, Tracks, OutError);
}

bool UAnimSequence::ValidateKeys(float FrameRate, int32 NumberOfKeys, const TArray<FBoneAnimationTrack>& Tracks, FString& OutError)
{
	if (!std::isfinite(FrameRate) || FrameRate <= 0.0f)
	{
		OutError = "Invalid frame rate";
		return false;
	}
	if (NumberOfKeys < 1)
	{
		OutError = "Animation has no keys";
		return false;
	}

	std::unordered_set<FName, FNameHash> Names;
	for (const FBoneAnimationTrack& Track : Tracks)
	{
		const FString TrackName = Track.BoneName.ToString();
		if (Track.BoneName.IsNone() || !Names.insert(Track.BoneName).second)
		{
			OutError = "Invalid or duplicate track bone: " + TrackName;
			return false;
		}
		const FRawAnimSequenceTrack& Keys = Track.InternalTrack;

		if (!IsValidKeyCount(Keys.PosKeys.Num(), NumberOfKeys) || !IsValidKeyCount(Keys.RotKeys.Num(), NumberOfKeys) ||
			!IsValidKeyCount(Keys.ScaleKeys.Num(), NumberOfKeys))
		{
			OutError = "Track key count must be 1 or NumberOfKeys: " + TrackName;
			return false;
		}
		for (const FVector& Key : Keys.PosKeys)
		{
			if (!IsFinite(Key)) { OutError = "Non-finite position key: " + TrackName; return false; }
		}
		for (const FQuat& Key : Keys.RotKeys)
		{
			// 회전 키는 단위 쿼터니언이어야 Slerp가 맞게 동작한다.
			const float LengthSquared = Key.X * Key.X + Key.Y * Key.Y + Key.Z * Key.Z + Key.W * Key.W;
			if (!IsFinite(Key) || std::abs(LengthSquared - 1.0f) > 1.0e-3f) { OutError = "Invalid rotation key: " + TrackName; return false; }
		}
		for (const FVector& Key : Keys.ScaleKeys)
		{
			if (!IsFinite(Key)) { OutError = "Non-finite scale key: " + TrackName; return false; }
		}
	}
	return true;
}
