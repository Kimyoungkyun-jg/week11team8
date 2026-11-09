#include "EnginePCH.h"
#include "Animation/AnimTwoWayBlendInstance.h"

UAnimTwoWayBlendInstance::UAnimTwoWayBlendInstance()
{
	auto A = std::make_unique<FAnimNode_SequencePlayer>();
	auto B = std::make_unique<FAnimNode_SequencePlayer>();
	PlayerA = A.get();
	PlayerB = B.get();

	auto Root = std::make_unique<FAnimNode_TwoWayBlend>();
	Root->A = std::move(A);
	Root->B = std::move(B);
	Root->AlphaVariable = BlendAlphaName();
	SetRootNode(std::move(Root));
}

void UAnimTwoWayBlendInstance::SetAnimationA(UAnimSequence* InSequence)
{
	InitializeGraph();
	PlayerA->SetSequence(InSequence);
}

void UAnimTwoWayBlendInstance::SetAnimationB(UAnimSequence* InSequence)
{
	InitializeGraph();
	PlayerB->SetSequence(InSequence);
}
