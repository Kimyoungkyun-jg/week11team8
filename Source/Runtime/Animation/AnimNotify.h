#pragma once

#include "Asset/AnimNotifyEvent.h"
#include "UObject/Object.h"
#include "AnimNotify.generated.h"

class USkeletalMeshComponent;
class UAnimSequence;

// Notify를 호출할 때 넘기는 정보. 정보가 늘어도 가상 함수의 시그니처가 바뀌지 않도록 구조체로 묶는다.
struct FAnimNotifyContext
{
	USkeletalMeshComponent* MeshComponent = nullptr;
	const UAnimSequence* Sequence = nullptr;
	const FAnimNotifyEvent* Event = nullptr;
	float Weight = 1.0f;
};

// 애니메이션의 특정 시각에 호출되는 동작의 기반 클래스.
// 시퀀스에는 Notify 클래스 이름과 속성(UPROPERTY)이 저장되고, 호출하는 동작은 파생 클래스의 Notify()가 정한다.
// 순수 가상 함수가 있어서 추상 클래스이므로 클래스 목록(Constructor가 있는 클래스)에는 파생 클래스만 나온다.
UCLASS()
class UAnimNotify : public UObject
{
	GENERATED_BODY()
public:
	virtual void Notify(const FAnimNotifyContext& Context) = 0;

	// 게임 월드가 아닌 곳(에디터 월드, 에디터 프리뷰)에서도 호출할지 정한다.
	// 게임플레이에 영향을 주는 Notify는 false로 둔다.
	virtual bool CanTriggerInEditorWorld() const { return false; }
};

// 호출될 때 로그만 남기는 확인용 Notify
UCLASS()
class UAnimNotify_Log : public UAnimNotify
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	FString Message;

	void Notify(const FAnimNotifyContext& Context) override;
	bool CanTriggerInEditorWorld() const override { return true; }
};

// 사운드 파일을 재생하는 Notify. BoneName이 있으면 그 본의 월드 위치에서, 없으면 컴포넌트 위치에서 재생한다.
// USoundWave*는 UPROPERTY로 저장할 수 없어서 경로 문자열로 들고 있다가 호출할 때 로드한다.
UCLASS()
class UAnimNotify_PlaySound : public UAnimNotify
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere)
	FString SoundPath;

	UPROPERTY(EditAnywhere)
	FString BoneName;

	UPROPERTY(EditAnywhere)
	float Volume = 1.0f;

	UPROPERTY(EditAnywhere)
	float Pitch = 1.0f;

	void Notify(const FAnimNotifyContext& Context) override;
private:
	// 불러오기에 실패한 경로. 같은 경로로는 호출될 때마다 다시 시도하지 않는다 (루프마다 로그와 객체가 쌓이는 것을 막는다).
	// 경로를 고치면 값이 달라져 다시 시도한다.
	FString FailedSoundPath;
	// 이미 경고한 본 이름. 없는 본 이름을 매번 경고하지 않는다.
	FString WarnedBoneName;
public:
	// 뷰어에서 소리를 들으며 Notify 위치를 맞출 수 있게 한다. 레벨 에디터 월드는 애니메이션을 틱하지 않으므로 프리뷰에서만 들린다.
	bool CanTriggerInEditorWorld() const override { return true; }
};
