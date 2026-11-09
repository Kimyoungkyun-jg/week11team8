#include "EnginePCH.h"
#include "BoxComponent.h"
#include "Physics/Collision.h"
#include "Render/LineBatcher.h"

FVector UBoxComponent::GetScaledBoxExtent() const
{
	const FVector S = GetWorldScale3D().GetAbs(); // 음수 스케일(뒤집기)도 크기는 양수
	return FVector(BoxExtent.X * S.X, BoxExtent.Y * S.Y, BoxExtent.Z * S.Z);	
}

FBox UBoxComponent::CalcLocalBounds() const
{
	return FBox{ BoxExtent * -1.0f, BoxExtent };
}

void UBoxComponent::DrawDebug(FLineBatcher* LineBatcher, const FVector4& Color) const
{
	// 엔진에 이미 있는 함수. 로컬 박스의 8꼭짓점을 월드 행렬(회전·스케일 포함)로 옮겨 그린다
	LineBatcher->AddOrientedBox(FBox{ BoxExtent * -1.0f, BoxExtent }, GetWorldMatrix(), Color);
	const FMatrix World = GetWorldMatrix();
	const FVector Corners[8] = {
		{ -BoxExtent.X, -BoxExtent.Y, -BoxExtent.Z }, { BoxExtent.X, -BoxExtent.Y, -BoxExtent.Z },
		{ BoxExtent.X, BoxExtent.Y, -BoxExtent.Z }, { -BoxExtent.X, BoxExtent.Y, -BoxExtent.Z },
		{ -BoxExtent.X, -BoxExtent.Y, BoxExtent.Z }, { BoxExtent.X, -BoxExtent.Y, BoxExtent.Z },
		{ BoxExtent.X, BoxExtent.Y, BoxExtent.Z }, { -BoxExtent.X, BoxExtent.Y, BoxExtent.Z }
	};
	constexpr int Faces[6][4] = {
		{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4},
		{1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}
	};
	for (const auto& Face : Faces)
		AddWireQuad(LineBatcher, World.TransformPosition(Corners[Face[0]]), World.TransformPosition(Corners[Face[1]]),
			World.TransformPosition(Corners[Face[2]]), World.TransformPosition(Corners[Face[3]]), Color);
}
