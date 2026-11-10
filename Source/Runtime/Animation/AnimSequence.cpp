#include "EnginePCH.h"
#include "AnimSequence.h"

float FAnimationCurveData::Evaluate(float TimeSeconds) const
{
	if (Keys.IsEmpty())
	{
		return DefaultValue;
	}
	if (TimeSeconds <= Keys[0].TimeSeconds)
	{
		return Keys[0].Value;
	}
	const int32 LastIndex = Keys.Num() - 1;
	if (TimeSeconds >= Keys[LastIndex].TimeSeconds)
	{
		return Keys[LastIndex].Value;
	}

	int32 Left = 0;
	int32 Right = LastIndex;
	while (Right - Left > 1)
	{
		const int32 Middle = Left + (Right - Left) / 2;
		if (Keys[Middle].TimeSeconds <= TimeSeconds)
			Left = Middle;
		else
			Right = Middle;
	}

	const FAnimationCurveKey& A = Keys[Left];
	const FAnimationCurveKey& B = Keys[Right];
	if (TimeSeconds == A.TimeSeconds)
	{
		return A.Value;
	}
	if (A.Interpolation == EAnimationCurveInterpolation::Constant)
	{
		return A.bConstantNext ? B.Value : A.Value;
	}

	const float Duration = B.TimeSeconds - A.TimeSeconds;
	const float Alpha = (TimeSeconds - A.TimeSeconds) / Duration;
	if (A.Interpolation == EAnimationCurveInterpolation::Linear)
	{
		return A.Value + (B.Value - A.Value) * Alpha;
	}

	// 탄젠트는 값/초 단위라서 정규화된 구간 길이에 맞춰 변환한다.
	const float Alpha2 = Alpha * Alpha;
	const float Alpha3 = Alpha2 * Alpha;
	return (2.0f * Alpha3 - 3.0f * Alpha2 + 1.0f) * A.Value
		+ (Alpha3 - 2.0f * Alpha2 + Alpha) * Duration * A.LeaveTangent
		+ (-2.0f * Alpha3 + 3.0f * Alpha2) * B.Value
		+ (Alpha3 - Alpha2) * Duration * B.ArriveTangent;
}
