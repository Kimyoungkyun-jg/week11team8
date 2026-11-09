#pragma once

#include "Asset/BlendSpace2D.h"
#include "SimpleBlendSpace2D.generated.h"

UCLASS()
class USimpleBlendSpace2D : public UBlendSpace2D
{
	GENERATED_BODY()
public:
	bool Configure(UAnimSequence* Idle, const std::array<UAnimSequence*, 8>& Walk,
		const std::array<UAnimSequence*, 8>& Jog)
	{
		constexpr std::array<float, 9> Directions{ -180.0f, -135.0f, -90.0f, -45.0f,
			0.0f, 45.0f, 90.0f, 135.0f, 180.0f };
		for (std::size_t Index = 0; Index < Directions.size(); ++Index)
		{
			const std::size_t Clip = Index == 8 ? 0 : Index;
			if (!AddSample(Directions[Index], 0.0f, Idle) ||
				!AddSample(Directions[Index], 300.0f, Walk[Clip]) ||
				!AddSample(Directions[Index], 600.0f, Jog[Clip])) return false;
		}
		return true;
	}
};
