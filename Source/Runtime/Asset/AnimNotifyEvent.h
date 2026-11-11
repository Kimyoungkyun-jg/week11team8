#pragma once

#include "Core/EngineString.h"

// 시퀀스의 특정 시각(초, 0~PlayLength)에 발생하는 이벤트
struct FAnimNotifyEvent
{
	FString Name;
	float TriggerTime = 0.0f;
};
