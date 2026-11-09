#include "EnginePCH.h"
#include "Animation/AnimSingleNodeInstance.h"

UAnimSingleNodeInstance::UAnimSingleNodeInstance()
{
	auto Root = std::make_unique<FAnimNode_SequencePlayer>();
	Player = Root.get();
	SetRootNode(std::move(Root));
}

void UAnimSingleNodeInstance::SetAnimationAsset(UAnimSequence* InSequence)
{
	// 컴포넌트가 이 호출 전에 Initialize로 소유자를 정해 두므로, 노드에 메시를 먼저 알린 뒤 시퀀스를 넣는다.
	InitializeGraph();
	Player->SetSequence(InSequence);
}
