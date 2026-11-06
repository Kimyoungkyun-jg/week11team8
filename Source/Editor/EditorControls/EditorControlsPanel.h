#pragma once
#include "Editor/EditorUI/EditorPanel.h"

#include "Editor/Gizmo/Gizmo.h"

#include "GameFramework/Actor/ParticleActor.h"
#include "GameFramework/Actor/StaticMeshActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "GameFramework/Actor/PointLightActor.h"
#include "GameFramework/Actor/SpotLightActor.h"
#include "GameFramework/Actor/RectLightActor.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/TextRenderActor.h"
#include "LevelSequence/LevelSequenceActor.h"
#include "GameFramework/Actor/ExponentialHeightFogActor.h"
#include "GameFramework/Actor/FireBallActor.h"
#include "GameFramework/Actor/ProjectileActor.h"
#include "GameFramework/Actor/ScoreboardManager.h"
#include "GameFramework/Actor/SkeletalMeshActor.h"
#include "GameFramework/Actor/DecalActor.h"
#include "GameFramework/Actor/FakeSpotLightActor.h"

class FViewportsPanel;

// 액터 생성과 카메라·기즈모 편집에 필요한 패널 상태를 보관한다.
class FEditorControlsPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime)override;
	void OnRender() override;
	void DrawToolbar();
	const char* GetPanelName() const override { return "Editor Controls"; }
	inline void SetGizmo(FGizmo* InGizmo) { Gizmo = InGizmo; }
	inline void SetWorld(UWorld* InWorld) { World = InWorld; }
	inline void SetViewportsPanel(FViewportsPanel* InPanel) { ViewportsPanel = InPanel; }
	inline void SetIsPlayingQuery(std::function<bool()> InQuery) { IsPlayingQuery = InQuery; }
	inline void SetSkipGizmoHotkeyQuery(std::function<bool()> Inquery) { SkipGizmoHotkeyQuery = std::move(Inquery); }

	float DeltaTime = 1.0f;
	UWorld* World = nullptr;
	FViewportsPanel* ViewportsPanel = nullptr;

	void AddActor(uint32 Index);
	// 편집 중인 View 카메라 앞에 액터를 만든다 (TEAM1). View가 없으면 원점이다.
	AActor* SpawnInFrontOfCamera(UClass* Class);

	// 목록은 알파벳순이다. Items와 Classes는 같은 인덱스끼리 짝이므로 순서를 함께 바꿔야 한다.
	// 기본 선택은 StaticMesh다.
	int32 SelectedIndex = 10;

	const char* Items[12] = {
		"Decal",
		"Empty Actor",
		"FakeSpotLight",
		"FireBall",
		"HeightFog",
		"LevelSequence",
		"Particle",
		"Projectile",
		"ScoreboardManager",
		"SkeletalMesh",
		"StaticMesh",
		"Text"};

	FGizmo* Gizmo = nullptr;
	int32 GizmoSelectedIndex = 0;
	const char* GizmoItems[3] ={"Location","Rotation","Scale"};

	int32 SpaceSelectedIndex = 0;
	const char* SpaceItems[2] ={"Local","World"};

	TArray<UClass*> Classes
	{
		ADecalActor::StaticClass(),
		AActor::StaticClass(),
		AFakeSpotLightActor::StaticClass(),
		AFireBallActor::StaticClass(),
		AExponentialHeightFogActor::StaticClass(),
		ALevelSequenceActor::StaticClass(),
		AParticleActor::StaticClass(),
		AProjectileActor::StaticClass(),
		AScoreboardManager::StaticClass(),
		ASkeletalMeshActor::StaticClass(),
		AStaticMeshActor::StaticClass(),
		ATextRenderActor::StaticClass()
	};

private:
	static constexpr float SectionGap = 10.0f;
	static constexpr float SubsectionGap = 4.0f;

    void DrawCameraProperties();
	std::function<bool()> IsPlayingQuery;
	std::function<bool()> SkipGizmoHotkeyQuery;
};

