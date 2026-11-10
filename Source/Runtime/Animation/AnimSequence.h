#pragma once
#include "UObject/Object.h"
#include "Asset/FbxImporter/AnimationData.h"
#include "Core/Types.h"
#include <functional>


class UAnimDataModel : public UObject
{
public:
	UAnimDataModel() : UObject(false) {}

	TArray<FBoneAnimationTrack> BoneAnimationTracks;
	float PlayLength = 0.0f;
	float FrameRate = 30.0f;
	int32 NumberOfFrames = 0; // 샘플 사이 구간 수
	int32 NumberOfKeys = 0; // 시작과 끝을 포함한 샘플 수
	TArray<FAnimationCurveData> CurveData;
	// CurveData 순서로 평가된 채널 값에서 본 트랙 순서의 엔진 로컬 포즈를 만든다.
	std::function<bool(float, const TArray<float>&, TArray<FMatrix>&)> ComposeCurvePose;

	// virtual const TArray<FBoneAnimationTrack>& GetBoneAnimationTracks() const override;
};

class UAnimSequenceBase //: public UAnimationAsset
{
public:
	UAnimSequenceBase() = default;
	UAnimSequenceBase(UAnimSequenceBase&&) noexcept = default;
	UAnimSequenceBase& operator=(UAnimSequenceBase&&) noexcept = default;
	virtual ~UAnimSequenceBase() = default;

	UAnimDataModel* GetDataModel() const { return AnimDataModel.get(); }
	TSharedPtr<const UAnimDataModel> GetSharedDataModel() const { return AnimDataModel; }


protected:
	TSharedPtr<UAnimDataModel> AnimDataModel;
};

class UAnimSequence : public UAnimSequenceBase
{
public:
	UAnimSequence() = default;
	UAnimSequence(FString InName, TUniquePtr<UAnimDataModel> InDataModel)
		: Name(std::move(InName))
	{
		AnimDataModel = std::move(InDataModel);
	}

	const FString& GetName() const { return Name; }

private:
	FString Name;
};
