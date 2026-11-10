#include "EnginePCH.h"
#include "Animation/AnimInstance.h"

void UAnimInstance::Play(
	const UAnimSequence* SequenceA, const UAnimSequence* SequenceB, bool bLoop, float InAlpha)
{
	SequencePlayerA.SetSequence(SequenceA, bLoop);
	SequencePlayerB.SetSequence(SequenceB, bLoop);

	if (SequenceB)
	{
		TwoWayBlend.A.SetLinkNode(&SequencePlayerA);
		TwoWayBlend.B.SetLinkNode(&SequencePlayerB);
		TwoWayBlend.SetAlpha(InAlpha);
		Root.SetLinkNode(&TwoWayBlend);
	}
	else
	{
		Root.SetLinkNode(&SequencePlayerA);
	}
	NativeInitializeAnimation();
}

void UAnimInstance::SetBlendAlpha(float InAlpha)
{
	TwoWayBlend.SetAlpha(InAlpha);
}

void UAnimInstance::NativeInitializeAnimation()
{
	Root.Initialize();
}

void UAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	const FAnimationUpdateContext Context{ DeltaSeconds };
	Root.Update(Context);
}

bool UAnimInstance::EvaluateLocalPose(const USkeletalMesh& Mesh, TArray<FMatrix>& OutLocalPose)
{
	FPoseContext Output{ Mesh, OutLocalPose };
	return Root.Evaluate(Output);
}
