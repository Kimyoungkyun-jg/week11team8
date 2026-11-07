#pragma once

#include "Containers/Array.h"
#include "Core/EngineString.h"
#include "Core/NameTypes.h"
#include "Math/Vector.h"
#include "Math/Quat.h"

// 본 하나의 애니메이션 키 (UE FRawAnimSequenceTrack).
// 값은 부모 본 기준 로컬 위치·회전·크기이고, 행렬로 합칠 때는 엔진 규약대로 S × R × T 순서다.
// 키는 시퀀스의 FrameRate 간격으로 균일하게 놓인다. 한 성분의 키 수는 시퀀스의 NumberOfKeys이거나,
// 값이 변하지 않으면 1개(상수 트랙)다. 성분마다 따로 1개가 될 수 있다.
struct FRawAnimSequenceTrack
{
	TArray<FVector> PosKeys;
	TArray<FQuat> RotKeys;
	TArray<FVector> ScaleKeys;
};

// 이름으로 본에 연결되는 트랙 (UE5 FBoneAnimationTrack).
// 트랙은 USkeleton의 본을 이름으로 가리키고, 재생할 때 대상 메시의 본 인덱스로 연결한다.
// 트랙이 없는 본은 대상의 기준 포즈(RefBonePose)를 쓴다.
// 이름은 FName이라 비교가 정수 비교다. FName 비교는 대소문자를 구분하지 않는다.
struct FBoneAnimationTrack
{
	FName BoneName;
	FRawAnimSequenceTrack InternalTrack;
};
