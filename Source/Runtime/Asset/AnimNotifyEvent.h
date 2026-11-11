#pragma once

#include "Core/EngineString.h"

class UAnimNotify;
class UAnimSequence;

// 시퀀스의 특정 시각(초, 0~PlayLength)에 발생하는 이벤트
struct FAnimNotifyEvent
{
	// 에디터 표시와 로그용 이름. 어떤 동작을 하는지는 Notify 객체가 정한다.
	FString Name;
	float TriggerTime = 0.0f;
	// 블렌딩 중 전파된 가중치가 이 값보다 작으면 호출하지 않는다. 양쪽 상태의 Notify가 모두 호출되지 않게 Notify마다 조절한다.
	float TriggerWeightThreshold = 0.00001f;
	// 호출할 Notify 객체. UAnimSequence가 Outer이고, 소멸은 엔진 종료 때 UObject와 함께 일괄 처리된다.
	UAnimNotify* Notify = nullptr;
};

// 노드 트리를 거쳐 UAnimInstance까지 올라온 Notify 한 건
struct FAnimNotifyQueueItem
{
	FAnimNotifyEvent Event;
	// 루트에서 이 Notify를 낸 노드까지의 블렌드 가중치를 곱한 값 (전이 중이 아니면 1)
	float Weight = 1.0f;
	// Notify가 든 시퀀스
	const UAnimSequence* Source = nullptr;
};
