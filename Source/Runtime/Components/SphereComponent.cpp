#include "EnginePCH.h"
#include "SphereComponent.h"
#include "Physics/Collision.h"
#include "Render/LineBatcher.h"

float USphereComponent::GetScaledSphereRadius() const
{
	const FVector S = GetWorldScale3D().GetAbs(); 
	return SphereRadius * std::max({S.X, S.Y, S.Z}); // 비균일 스케일이면 가장 큰 축
}

FBox USphereComponent::CalcLocalBounds() const
{
	const FVector R(SphereRadius, SphereRadius, SphereRadius);
	return FBox{ R * -1.0f, R };
}

void USphereComponent::DrawDebug(FLineBatcher* LineBatcher, const FVector4& Color) const
{
	const FVector C = GetWorldLocation();
	const float R = GetScaledSphereRadius();
	constexpr float TwoPi = 2.0f * PI;
	AddWireArc(LineBatcher, C, FVector(1, 0, 0), FVector(0, 1, 0), R, 0, TwoPi, Color);
	AddWireArc(LineBatcher, C, FVector(1, 0, 0), FVector(0, 0, 1), R, 0, TwoPi, Color);
	AddWireArc(LineBatcher, C, FVector(0, 1, 0), FVector(0, 0, 1), R, 0, TwoPi, Color);
	constexpr int Latitudes = 6;
	constexpr int Longitudes = 12;
	const auto Point = [&](float Latitude, float Longitude)
	{
		const float Ring = std::cos(Latitude);
		return C + FVector(Ring * std::cos(Longitude), Ring * std::sin(Longitude), std::sin(Latitude)) * R;
	};
	for (int Lat = 0; Lat < Latitudes; ++Lat)
		for (int Lon = 0; Lon < Longitudes; ++Lon)
		{
			const float Lat0 = -PI * 0.5f + PI * Lat / Latitudes;
			const float Lat1 = -PI * 0.5f + PI * (Lat + 1) / Latitudes;
			const float Lon0 = TwoPi * Lon / Longitudes;
			const float Lon1 = TwoPi * (Lon + 1) / Longitudes;
			AddWireQuad(LineBatcher, Point(Lat0, Lon0), Point(Lat0, Lon1),
				Point(Lat1, Lon1), Point(Lat1, Lon0), Color);
		}
}
