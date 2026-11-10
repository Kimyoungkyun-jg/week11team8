#pragma once

#include "Core/EngineString.h"
#include "Core/Types.h"
#include <vector>

enum class EAnimGraphInput { Constant, Speed, Direction, JumpRequested, Falling, Grounded, MovingOnGround, SequenceEnding };
enum class EAnimGraphPose { Sequence, BlendSpace1D, BlendSpace2D };
enum class EAnimGraphCompare { Always, Greater, GreaterEqual, Less, LessEqual };

struct FAnimGraphPlayerNode
{
	int32 Id = 0;
	EAnimGraphPose Pose = EAnimGraphPose::Sequence;
	FString AssetPath;
	EAnimGraphInput InputX = EAnimGraphInput::Constant;
	EAnimGraphInput InputY = EAnimGraphInput::Constant;
	float ConstantX = 0.0f;
	float ConstantY = 0.0f;
	float PlayRate = 1.0f;
	bool bLoop = true;
	float X = 0.0f;
	float Y = 0.0f;
};

struct FAnimGraphState
{
	int32 Id = 0;
	FString Name;
	float X = 0.0f;
	float Y = 0.0f;
	EAnimGraphPose Pose = EAnimGraphPose::Sequence;
	FString AssetPath;
	EAnimGraphInput InputX = EAnimGraphInput::Constant;
	EAnimGraphInput InputY = EAnimGraphInput::Constant;
	float ConstantX = 0.0f;
	float ConstantY = 0.0f;
	float PlayRate = 1.0f;
	bool bLoop = true;
	bool bPoseConnected = true;
	int32 OutputPlayerId = 0;
	std::vector<FAnimGraphPlayerNode> Players;
	float PlayerOffsetX = 0.0f;
	float PlayerOffsetY = 0.0f;
	float OutputOffsetX = 0.0f;
	float OutputOffsetY = 0.0f;
	float SpeedOffsetX = 0.0f;
	float SpeedOffsetY = 0.0f;
	float DirectionOffsetX = 0.0f;
	float DirectionOffsetY = 0.0f;
};

struct FAnimGraphTransition
{
	int32 Id = 0;
	int32 From = 0;
	int32 To = 0;
	EAnimGraphInput Input = EAnimGraphInput::Speed;
	EAnimGraphCompare Compare = EAnimGraphCompare::Greater;
	float Threshold = 0.0f;
	float BlendTime = 0.1f;
};

struct FAnimGraphAsset
{
	FString SkeletonPath;
	bool bRootPoseConnected = true;
	bool bInvertDirection = false;
	int32 EntryState = 0;
	std::vector<FAnimGraphState> States;
	std::vector<FAnimGraphTransition> Transitions;

	bool Validate(FString& Error) const;
	static bool Load(const FString& Path, FAnimGraphAsset& Out, FString& Error);
	bool Save(const FString& Path, FString& Error) const;
};
