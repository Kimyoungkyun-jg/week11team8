#include "EnginePCH.h"
#include "Editor/HitoriEd/EditorResourcePaths.h"
#include "Editor/Settings/SettingsPanel.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

#include "Engine/World.h"
#include "Editor/Viewports/ViewportsPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/GameHUD.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Editor/EditorUI/EditorSort.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectHash.h"

namespace
{
	// 예전 editor.ini의 전역 표시 토글 키를 대응하는 뷰별 Show 플래그로 바꾼다.
	bool FViewShowFlags::* FindLegacyShowFlag(const std::string& Key)
	{
		if (Key == "DrawPrimitives") return &FViewShowFlags::bPrimitives;
		if (Key == "DrawBoundingBox") return &FViewShowFlags::bBounds;
		if (Key == "ShowUUID") return &FViewShowFlags::bUUID;
		if (Key == "DrawBatchLine") return &FViewShowFlags::bGrid;
		if (Key == "DrawCascadeBounds") return &FViewShowFlags::bCascadeBounds;
		if (Key == "EnableFXAA") return &FViewShowFlags::bFXAA;
		if (Key == "ShowVignette") return &FViewShowFlags::bVignette;
		if (Key == "ShowCameraOverlay") return &FViewShowFlags::bCameraOverlay;
		return nullptr;
	}
}

// 종료 시 렌더·에디터·뷰포트 설정을 함께 저장한다.
FSettingsPanel::~FSettingsPanel()
{
	ViewportsPanel = nullptr;
	SaveSettings();
}

// 설정 파일을 읽어 패널 상태를 초기화한다.
bool FSettingsPanel::Init()
{
	LoadSettings();
	USkeletalMeshComponent::SetSkinningMode(Settings.RenderingPath == ERenderingPath::Forward
		? Settings.SkinningMode : ESkinningMode::CPU);
	return true;
}


// 설정 패널의 프레임 갱신 진입점이다.
void FSettingsPanel::Tick(float DeltaTime)
{
}

// ImGui 조작으로 렌더·카메라 설정과 저장·복원 요청을 처리한다.
void FSettingsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(400.0f), EditorUIScale::Px(300.0f)), ImGuiCond_FirstUseEver);
	ImGui::Begin("Settings");
	// 안내 문구가 패널 폭을 넘으면 잘리지 않고 다음 줄로 내려간다.
	ImGui::PushTextWrapPos(0.0f);

	//////////////////////////////////////////////////////////

	// 렌더러 전역 옵션. 뷰 모드와 Show 플래그는 각 뷰포트 툴바에서 바꾼다.
	ImGui::SeparatorText("Rendering");
	ImGui::TextDisabled("View mode and show flags are set in each viewport toolbar.");

	const char* RenderingPaths[] = { "Forward", "Deferred" };
	int RenderingPathIndex = static_cast<int>(Settings.RenderingPath);
	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	if (ImGui::Combo("Rendering Path", &RenderingPathIndex, RenderingPaths, IM_ARRAYSIZE(RenderingPaths)))
		Settings.RenderingPath = static_cast<ERenderingPath>(RenderingPathIndex);
	const char* SkinningModes[] = { "CPU", "GPU" };
	int SkinningModeIndex = static_cast<int>(Settings.SkinningMode);
	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	if (ImGui::Combo("Skinning Mode", &SkinningModeIndex, SkinningModes, IM_ARRAYSIZE(SkinningModes)))
		Settings.SkinningMode = static_cast<ESkinningMode>(SkinningModeIndex);
	USkeletalMeshComponent::SetSkinningMode(Settings.RenderingPath == ERenderingPath::Forward
		? Settings.SkinningMode : ESkinningMode::CPU);
	if (Settings.RenderingPath != ERenderingPath::Forward && Settings.SkinningMode == ESkinningMode::GPU)
		ImGui::TextDisabled("GPU skinning applies to Forward rendering only.");

	// Unlit은 뷰포트 뷰 모드에서 고른다. 여기서는 조명 셰이딩 방식만 정한다.
	const char* LightingModels[] = { "Gouraud", "Lambert", "Phong" };
	int LightingModelIndex = static_cast<int>(Settings.LightingModel);
	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	if (ImGui::Combo("Lighting Model", &LightingModelIndex, LightingModels, IM_ARRAYSIZE(LightingModels)))
		Settings.LightingModel = static_cast<ELightingModel>(LightingModelIndex);

	const char* DecalMethods[] = { "Mesh Re-rendering", "Depth Reconstruction" };
	int DecalMethodIndex = static_cast<int>(Settings.DecalRenderMethod);
	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	if (ImGui::Combo("Decal Method", &DecalMethodIndex, DecalMethods, IM_ARRAYSIZE(DecalMethods)))
		Settings.DecalRenderMethod = static_cast<EDecalRenderMethod>(DecalMethodIndex);

	const char* LightCullingModes[] = { "Off", "Tiled", "Clustered" };
	int LightCullingModeIndex = static_cast<int>(Settings.LightCullingMode);
	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	if (ImGui::Combo("Light Culling", &LightCullingModeIndex,
		LightCullingModes, IM_ARRAYSIZE(LightCullingModes)))
	{
		Settings.LightCullingMode = static_cast<ELightCullingMode>(LightCullingModeIndex);
	}
	ImGui::TextDisabled("Forward Lambert/Phong only; Off evaluates all visible local lights.");

	//////////////////////////////////////////////////////////

	ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(SectionGap)));
	ImGui::SeparatorText("Post Process");

	ImGui::Checkbox("Gamma Correction", &Settings.bGammaCorrection);
	ImGui::BeginDisabled(!Settings.bGammaCorrection);
	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	ImGui::SliderFloat("Display Gamma", &Settings.DisplayGamma, 1.0f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::EndDisabled();

	if (ImGui::TreeNode("Camera Overlay Debug"))
	{
		FCameraOverlayDebug& D = GCameraOverlayDebug;
		ImGui::Checkbox("Override (PIE)", &D.bOverride);
		ImGui::ColorEdit3("Fade Color", D.FadeColor);
		ImGui::SliderFloat("Fade Amount", &D.FadeAmount, 0.0f, 1.0f);
		ImGui::SliderFloat("Letterbox", &D.LetterboxAmount, 0.0f, 1.0f);
		ImGui::SliderFloat("Letterbox Aspect", &D.LetterboxAspect, 1.0f, 3.0f, "%.2f");
		ImGui::SliderFloat("Vignette", &D.VignetteIntensity, 0.0f, 2.0f);
		ImGui::TreePop();
	}

	//////////////////////////////////////////////////////////

	// 에디터 수치 설정 (Values)
	ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(SectionGap)));
	ImGui::SeparatorText("Editor Settings");

	UCameraComponent* CamCom = World->GetMainCamera()->GetCameraComponent();

	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	ImGui::SliderFloat("Camera Rotate Sensitivity", &Settings.MouseSensitivity, 0.01f, 1.0f, "%.2f");
	CamCom->SetMouseSensitivity(Settings.MouseSensitivity);

	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	ImGui::SliderFloat("Camera Speed", &Settings.CameraSpeed, 0.1f, 10.0f, "%.2f");
	CamCom->SetMoveSpeed(Settings.CameraSpeed);

	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	ImGui::SliderInt("Grid Spacing", &Settings.GridSpacing, 1, 100);

	//////////////////////////////////////////////////////////
	DrawShadowMapSettings();
	//////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////
	DrawDefaultPawnSettings();
	DrawGameFrameworkSettings();

	//////////////////////////////////////////////////////////
	ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(SectionGap)));
	ImGui::SeparatorText("Load Settings");

	if (ImGui::Button("Load Settings"))
	{
		LoadSettings();
		ApplyViewportSettings();
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Load settings from editor.ini");
	}

	ImGui::SameLine();

	if (ImGui::Button("Save Settings"))
	{
		SaveSettings();
	}

	//////////////////////////////////////////////////////////

	ImGui::PopTextWrapPos();
	ImGui::End();
}

void FSettingsPanel::DrawGameFrameworkSettings()
{
	if (!World) return;
	ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(SectionGap)));
	ImGui::SeparatorText("Scene Game Framework");
	ImGui::TextDisabled("Use Save Scene to persist. Takes effect on next Play.");
	ImGui::BeginDisabled(World->IsGameWorld());
	const auto ClassPicker = [](const char* Label, const FString& CurrentName, UClass* Base,
		bool bAllowDefault, const auto& SetClass)
	{
		const char* Preview = CurrentName.empty() ? "GameMode Default" : CurrentName.c_str();
		ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
		if (!ImGui::BeginCombo(Label, Preview)) return;
		if (bAllowDefault && ImGui::Selectable("GameMode Default", CurrentName.empty())) SetClass(nullptr);
		TArray<UClass*> Classes;
		GetDerivedClasses(Base, Classes);
		Classes.Add(Base);
		EditorSort::SortByName(Classes, [](const UClass* Class) { return Class ? Class->Name : FString(); });
		for (UClass* Class : Classes)
			if (Class && Class->Constructor && ImGui::Selectable(Class->Name.c_str(), CurrentName == Class->Name))
				SetClass(Class);
		ImGui::EndCombo();
	};
	ClassPicker("Game Mode", World->GetGameModeClassName(), AGameMode::StaticClass(), false,
		[this](UClass* Class) { World->SetGameModeClass(Class); });
	ClassPicker("Player Pawn", World->GetPlayerPawnClassName(), APawn::StaticClass(), true,
		[this](UClass* Class) { World->SetPlayerPawnClass(Class); });
	ClassPicker("Player Controller", World->GetPlayerControllerClassName(), APlayerController::StaticClass(), true,
		[this](UClass* Class) { World->SetPlayerControllerClass(Class); });
	ClassPicker("HUD", World->GetHUDClassName(), AGameHUD::StaticClass(), true,
		[this](UClass* Class) { World->SetHUDClass(Class); });
	ImGui::EndDisabled();
}

// 알려진 설정 섹션을 editor.ini에 함께 기록해 뷰포트 값 유실을 막는다.
bool FSettingsPanel::SaveSettings() const
{
	FEditorSettings Snapshot = Settings;
	ReadViewportSettings(Snapshot);
	// 사용자 설정 폴더는 처음 저장할 때 만든다.
	std::error_code SettingsDirError;
	std::filesystem::create_directories(EditorResourcePaths::SettingsFolder, SettingsDirError);
	std::ofstream File(EditorResourcePaths::EditorSettingsFile);

	if (!File.is_open())
	{
		HTR_LOG(Error, "Failed to open editor.ini for saving.");
		return false;
	}

	File << "[Rendering]\n";
	File << "Wireframe=" << Settings.bWireframe << "\n";
	File << "CacheShadows=" << Settings.bCacheShadows << "\n";
	File << "PcfMode=" << static_cast<uint32_t>(Settings.PcfMode) << "\n";
	File << "SceneDepthMaxRange=" << Snapshot.SceneDepthMaxRange << "\n";

	File << "GammaCorrection=" << Settings.bGammaCorrection << "\n";
	File << "DisplayGamma=" << Settings.DisplayGamma << "\n";

	File << "LightCullingMode=" << (Settings.LightCullingMode == ELightCullingMode::Off ? "Off" : Settings.LightCullingMode == ELightCullingMode::Tiled ? "Tiled" : "Clustered") << "\n";
	File << "RenderingPath=" << (Settings.RenderingPath == ERenderingPath::Forward ? "Forward" : "Deferred") << "\n";
	File << "SkinningMode=" << (Settings.SkinningMode == ESkinningMode::GPU ? "GPU" : "CPU") << "\n";
	File << "LightingModel=" << (Settings.LightingModel == ELightingModel::Gouraud ? "Gouraud" :
		Settings.LightingModel == ELightingModel::Lambert ? "Lambert" :
		Settings.LightingModel == ELightingModel::Unlit ? "Unlit" : "Phong") << "\n";
	File << "DecalRenderMethod=" << (Settings.DecalRenderMethod == EDecalRenderMethod::MeshRerendering ? "MeshRerendering" : "DepthReconstruction") << "\n";
	File << "\n";

	File << "[Editor]\n";
	File << "CameraMoveSpeed=" << Settings.CameraSpeed << "\n";
	File << "CameraRotateSensitivity=" << Settings.MouseSensitivity << "\n";
	File << "GridSpacing=" << Settings.GridSpacing << "\n";
	File << "\n";

	File << "[MultipleViewports]\n";
	File << "Horizontal=" << Snapshot.MultipleViewportsHorizontal << "\n";
	File << "Vertical=" << Snapshot.MultipleViewportsVertical << "\n";
	File << "Layout=" << (Snapshot.bMultipleViewportsSingle ? "Single" : "Quad") << "\n";
	File << "SingleViewIndex=" << Snapshot.MultipleViewportsSingleViewIndex << "\n";

	for (int32 Index = 0; Index < 4; ++Index)
    {
        File << "View" << Index << "Fov=" << Snapshot.ViewFov[Index] << "\n";
        File << "View" << Index << "OrthoWidth=" << Snapshot.ViewOrthoWidth[Index] << "\n";
        File << "View" << Index << "Preset=" << Snapshot.ViewPreset[Index] << "\n";
        File << "View" << Index << "Wireframe=" << Snapshot.ViewWireframe[Index] << "\n";
        File << "View" << Index << "Mode=" << Snapshot.ViewMode[Index] << "\n";
        for (const FViewShowFlagInfo& Info : ViewShowFlagInfos)
            File << "View" << Index << "Show" << Info.Key << "=" << (Snapshot.ViewShowFlags[Index].*Info.Member ? 1 : 0) << "\n";
        File << "View" << Index << "LocationX=" << Snapshot.ViewLocation[Index].X << "\n";
        File << "View" << Index << "LocationY=" << Snapshot.ViewLocation[Index].Y << "\n";
        File << "View" << Index << "LocationZ=" << Snapshot.ViewLocation[Index].Z << "\n";
        File << "View" << Index << "RotationX=" << Snapshot.ViewRotation[Index].X << "\n";
        File << "View" << Index << "RotationY=" << Snapshot.ViewRotation[Index].Y << "\n";
        File << "View" << Index << "RotationZ=" << Snapshot.ViewRotation[Index].Z << "\n";
        File << "View" << Index << "RotationW=" << Snapshot.ViewRotation[Index].W << "\n";
    }

	File << "DirectionalShadowMapSizeLevel=" << Settings.DirectionalShadowMapSizeLevel << '\n';
	File << "PointShadowMapSizeLevel=" << Settings.PointShadowMapSizeLevel << '\n';
	File << "SpotShadowMapSizeLevel=" << Settings.SpotShadowMapSizeLevel << '\n';

    File.close();
	return true;
}

// 파일의 키를 파싱해 렌더·카메라·레이아웃 설정을 복원한다.
bool FSettingsPanel::LoadSettings()
{
	std::ifstream File(EditorResourcePaths::EditorSettingsFile);
	if (!File.is_open())
	{
		return false;
	}

	uint8 LocationComponentMasks[4]{};
	uint8 RotationComponentMasks[4]{};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Settings.bViewLocationSaved[Index] = false;
		Settings.bViewRotationSaved[Index] = false;
	}

	std::string Line;
	while (std::getline(File, Line))
	{
		if (Line.empty() || Line[0] == ';' || Line[0] == '[') continue;

		std::istringstream Iss(Line);
		std::string Key;
		if (std::getline(Iss, Key, '='))
		{
			std::string ValueStr;
			if (std::getline(Iss, ValueStr))
			{
				// 새 View 키는 NaN·잘못된 숫자·범위 밖 값을 무시한다.
                for (int32 Index = 0; Index < 4; ++Index)
                {
                    FString Prefix = "View";
                    Prefix += static_cast<char>('0' + Index);
                    std::istringstream Number(ValueStr);
                    float Value = 0;
                    if (!(Number >> Value) || !std::isfinite(Value)) continue;
                    if (Key == Prefix + "Fov" && Value >= 1 && Value <= 179)
                        Settings.ViewFov[Index] = Value;
                    else if (Key == Prefix + "OrthoWidth" && Value >= 0.01f)
                        // 파일 값은 절대 상한으로 먼저 제한하고, 실제 View 비율 상한은 Adapter가 적용한다.
                        Settings.ViewOrthoWidth[Index] = FMath::Clamp(Value, 0.01f, 1000000.0f);
                    else if (Key == Prefix + "Preset" && Value >= 0 && Value <= 7 && Value == static_cast<int32>(Value))
                        Settings.ViewPreset[Index] = static_cast<int32>(Value);
                    else if (Key == Prefix + "Wireframe" && (Value == 0 || Value == 1))
                        Settings.ViewWireframe[Index] = static_cast<int32>(Value);
					else if (Key == Prefix + "Mode" && Value >= 0 &&
						Value <= static_cast<int32>(EViewportMode::Unlit) && Value == static_cast<int32>(Value))
						Settings.ViewMode[Index] = static_cast<int32>(Value);
					else if (Key.rfind(Prefix + "Show", 0) == 0 && (Value == 0 || Value == 1))
					{
						for (const FViewShowFlagInfo& Info : ViewShowFlagInfos)
							if (Key == Prefix + "Show" + Info.Key)
								Settings.ViewShowFlags[Index].*Info.Member = Value != 0;
					}
                    else if (Key == Prefix + "LocationX")
                    {
                        Settings.ViewLocation[Index].X = Value;
                        LocationComponentMasks[Index] |= 1 << 0;
                    }
                    else if (Key == Prefix + "LocationY")
                    {
                        Settings.ViewLocation[Index].Y = Value;
                        LocationComponentMasks[Index] |= 1 << 1;
                    }
                    else if (Key == Prefix + "LocationZ")
                    {
                        Settings.ViewLocation[Index].Z = Value;
                        LocationComponentMasks[Index] |= 1 << 2;
                    }
                    else if (Key == Prefix + "RotationX")
                    {
                        Settings.ViewRotation[Index].X = Value;
                        RotationComponentMasks[Index] |= 1 << 0;
                    }
                    else if (Key == Prefix + "RotationY")
                    {
                        Settings.ViewRotation[Index].Y = Value;
                        RotationComponentMasks[Index] |= 1 << 1;
                    }
                    else if (Key == Prefix + "RotationZ")
                    {
                        Settings.ViewRotation[Index].Z = Value;
                        RotationComponentMasks[Index] |= 1 << 2;
                    }
                    else if (Key == Prefix + "RotationW")
                    {
                        Settings.ViewRotation[Index].W = Value;
                        RotationComponentMasks[Index] |= 1 << 3;
                    }
                }
                if (Key == "Wireframe") Settings.bWireframe = std::stoi(ValueStr);
				// 예전 전역 토글 키는 모든 뷰의 Show 플래그 초기값이 된다. 뒤에 오는 뷰별 키가 있으면 그 값이 이긴다.
				else if (bool FViewShowFlags::* Legacy = FindLegacyShowFlag(Key))
				{
					for (FViewShowFlags& Flags : Settings.ViewShowFlags)
						Flags.*Legacy = std::stoi(ValueStr) != 0;
				}
				else if (Key == "CacheShadows") Settings.bCacheShadows = std::stoi(ValueStr);
				else if (Key == "PcfMode")
				{
					const int Mode = std::stoi(ValueStr);
					if (Mode >= 0 && Mode <= static_cast<int>(EPcfMode::PCSS))
						Settings.PcfMode = static_cast<EPcfMode>(Mode);
				}
				else if (Key == "SceneDepthMaxRange") Settings.SceneDepthMaxRange = std::stof(ValueStr);

				else if (Key == "GammaCorrection")   Settings.bGammaCorrection = std::stoi(ValueStr);
				else if (Key == "DisplayGamma")      Settings.DisplayGamma = std::stof(ValueStr);

				else if (Key == "LightCullingMode")
				{
					if (ValueStr == "Off") Settings.LightCullingMode = ELightCullingMode::Off;
					else if (ValueStr == "Tiled") Settings.LightCullingMode = ELightCullingMode::Tiled;
					else if (ValueStr == "Clustered") Settings.LightCullingMode = ELightCullingMode::Clustered;
				}
				else if (Key == "EnableTiledLightCulling" || Key == "EnableClusteredLightCulling")
				{
					Settings.LightCullingMode = std::stoi(ValueStr) != 0 ? ELightCullingMode::Tiled : ELightCullingMode::Off;
				}
				else if (Key == "RenderingPath")
				{
					if (ValueStr == "Forward") Settings.RenderingPath = ERenderingPath::Forward;
					else if (ValueStr == "Deferred") Settings.RenderingPath = ERenderingPath::Deferred;
				}
				else if (Key == "SkinningMode")
				{
					if (ValueStr == "GPU") Settings.SkinningMode = ESkinningMode::GPU;
					else if (ValueStr == "CPU") Settings.SkinningMode = ESkinningMode::CPU;
				}
				else if (Key == "LightingModel")
				{
					if (ValueStr == "Gouraud") Settings.LightingModel = ELightingModel::Gouraud;
					else if (ValueStr == "Lambert") Settings.LightingModel = ELightingModel::Lambert;
					else if (ValueStr == "Phong") Settings.LightingModel = ELightingModel::Phong;
					else if (ValueStr == "Unlit") Settings.LightingModel = ELightingModel::Gouraud; // 이제 뷰 모드에서 고른다
				}
				else if (Key == "DecalRenderMethod")
				{
					if (ValueStr == "DepthReconstruction") Settings.DecalRenderMethod = EDecalRenderMethod::DepthReconstruction;
					else if (ValueStr == "MeshRerendering") Settings.DecalRenderMethod = EDecalRenderMethod::MeshRerendering;
				}

				else if (Key == "CameraMoveSpeed") Settings.CameraSpeed = std::stof(ValueStr);
				else if (Key == "CameraRotateSensitivity") Settings.MouseSensitivity = std::stof(ValueStr);
				else if (Key == "GridSpacing") Settings.GridSpacing = std::stoi(ValueStr);
				else if (Key == "Horizontal") Settings.MultipleViewportsHorizontal = std::stof(ValueStr);
				else if (Key == "Vertical") Settings.MultipleViewportsVertical = std::stof(ValueStr);
				else if (Key == "Layout") Settings.bMultipleViewportsSingle = ValueStr == "Single";
				else if (Key == "SingleViewIndex")
				{
					const int32 ViewIndex = std::stoi(ValueStr);
					Settings.MultipleViewportsSingleViewIndex = ViewIndex >= 0 && ViewIndex < 4 ? ViewIndex : 0;
				}
				else if (Key == "DirectionalShadowMapSizeLevel")
				{
					Settings.DirectionalShadowMapSizeLevel = std::stoi(ValueStr);
				}
				else if (Key == "PointShadowMapSizeLevel")
				{
					Settings.PointShadowMapSizeLevel = std::stoi(ValueStr);
				}
				else if (Key == "SpotShadowMapSizeLevel")
				{
					Settings.SpotShadowMapSizeLevel = std::stoi(ValueStr);
				}
			}
		}
	}

	File.close();
	if (!std::isfinite(Settings.SceneDepthMaxRange))
		Settings.SceneDepthMaxRange = 50.0f;
	Settings.SceneDepthMaxRange = FMath::Clamp(Settings.SceneDepthMaxRange, 0.1f, 10000.0f);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Settings.bViewLocationSaved[Index] = LocationComponentMasks[Index] == 0x7;
		if (RotationComponentMasks[Index] == 0xF)
		{
			const FQuat& Rotation = Settings.ViewRotation[Index];
			const float LengthSquared = Rotation.X * Rotation.X + Rotation.Y * Rotation.Y +
				Rotation.Z * Rotation.Z + Rotation.W * Rotation.W;
			if (LengthSquared > 1.0e-12f)
			{
				Settings.ViewRotation[Index] = Rotation.Normalized();
				Settings.bViewRotationSaved[Index] = true;
			}
		}
	}
	Settings.DirectionalShadowMapSizeLevel = FMath::Clamp(Settings.DirectionalShadowMapSizeLevel, 0, 4);
	Settings.PointShadowMapSizeLevel = FMath::Clamp(Settings.PointShadowMapSizeLevel, 0, 4);
	Settings.SpotShadowMapSizeLevel = FMath::Clamp(Settings.SpotShadowMapSizeLevel, 0, 4);
	ApplyViewportSettings();
	return true;
}

// 뷰포트 설정 복사
void FSettingsPanel::ReadViewportSettings(FEditorSettings& Out) const
{
	if (!ViewportsPanel)
		return;

	for (int32 Index = 0; Index < 4; ++Index)
	{
		FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(Index);
		if (!Client)
			continue;

		Out.ViewFov[Index] = Client->GetViewFOV();
		Out.ViewOrthoWidth[Index] = Client->GetOrthoWidth();
		Out.ViewWireframe[Index] = Client->IsWireframe() ? 1 : 0;
		Out.ViewMode[Index] = static_cast<int32>(Client->GetViewportMode());
		Out.ViewShowFlags[Index] = Client->GetShowFlags();
		if (Index == 0)
			Out.SceneDepthMaxRange = Client->GetMaxRange();
		Out.ViewLocation[Index] = Client->GetViewLocation();
		Out.ViewRotation[Index] = Client->GetViewRotation().Quaternion();
	}
}

// 종료 시 설정 갱신
void FSettingsPanel::CaptureViewportSettings() { ReadViewportSettings(Settings); }

// 뷰포트 설정 적용
void FSettingsPanel::ApplyViewportSettings()
{
	if (!ViewportsPanel)
		return;
	ApplySceneDepthRange();

	for (int32 Index = 0; Index < 4; ++Index)
	{
		FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(Index);
		if (!Client)
			continue;

		if (Settings.ViewFov[Index] > 0.0f)
			Client->SetViewFOV(Settings.ViewFov[Index]);
		if (Settings.ViewWireframe[Index] >= 0)
			Client->SetViewportMode(Settings.ViewWireframe[Index] == 1 ? EViewportMode::Wireframe : EViewportMode::Solid);
		if (Settings.ViewMode[Index] >= 0)
			Client->SetViewportMode(static_cast<EViewportMode>(Settings.ViewMode[Index]));
		Client->GetShowFlags() = Settings.ViewShowFlags[Index];
	}
}

void FSettingsPanel::ApplySceneDepthRange()
{
	if (!ViewportsPanel)
		return;

	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (FEditorViewportClient* Client = ViewportsPanel->GetViewportClient(Index))
			Client->SetMaxRange(Settings.SceneDepthMaxRange);
	}
}

void FSettingsPanel::DrawDefaultPawnSettings()
{
    ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(SectionGap)));
    ImGui::SeparatorText("DefaultPawn Settings");
    ADefaultPawn* Pawn = World ? Cast<ADefaultPawn>(World->GetPlayerPawn()) : nullptr;
    if (!Pawn)
    {
        ImGui::TextDisabled("Start PIE with a DefaultPawn to edit these settings.");
        return;
    }

    const auto DrawMeshPicker = [](const char* Label, UStaticMesh*& SelectedMesh)
    {
        const auto GetMeshDisplayName = [](const UStaticMesh* Mesh) -> FString
        {
            if (!Mesh) return "None";
            const fs::path Path(Mesh->GetPath());
            return Path.stem() == "Atlas"
                ? Path.parent_path().filename().string()
                : Path.stem().string();
        };
        const FString Preview = GetMeshDisplayName(SelectedMesh);
        bool bChanged = false;
        const bool bOpen = ImGui::BeginCombo(Label, Preview.c_str());
        if (SelectedMesh && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", SelectedMesh->GetPath().c_str());
        if (bOpen)
        {
            if (ImGui::Selectable("None", SelectedMesh == nullptr))
            {
                SelectedMesh = nullptr;
                bChanged = true;
            }
            const TArray<UStaticMesh*> Meshes = EditorSort::CollectSorted<UStaticMesh>(
                TObjectIterator<UStaticMesh>(), GetMeshDisplayName,
                [](const UStaticMesh* M) { return !M->IsRetired(); });
            for (UStaticMesh* Mesh : Meshes)
            {
                const FString Name = GetMeshDisplayName(Mesh);
                const bool bSelected = Mesh == SelectedMesh;
                ImGui::PushID(Mesh);
                if (ImGui::Selectable(Name.c_str(), bSelected))
                {
                    SelectedMesh = Mesh;
                    bChanged = true;
                }
                if (bSelected) ImGui::SetItemDefaultFocus();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Mesh->GetPath().c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        return bChanged;
    };

    UStaticMeshComponent* PawnMesh = Pawn->GetStaticMeshComponent();
    if (PawnMesh)
    {
        UStaticMesh* Mesh = PawnMesh->GetStaticMesh();
        if (DrawMeshPicker("Pawn Mesh", Mesh)) PawnMesh->SetStaticMesh(Mesh);
    }
}

void FSettingsPanel::DrawShadowMapSettings()
{
	ImGui::Dummy(ImVec2(0.0f, EditorUIScale::Px(SectionGap)));

	ImGui::SeparatorText("ShadowMap Settings");
	ImGui::Checkbox("Cache Shadows", &Settings.bCacheShadows);

	const char* QualityLevelStr[] = { "Very Low", "Low", "Middle", "High", "Very High" };
	
	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	if (ImGui::Combo("Directional", &Settings.DirectionalShadowMapSizeLevel, QualityLevelStr, IM_ARRAYSIZE(QualityLevelStr)))
	{
		Settings.DirectionalShadowMapSizeLevel = FMath::Clamp(Settings.DirectionalShadowMapSizeLevel, 0, IM_ARRAYSIZE(QualityLevelStr) - 1);
	}

	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	if (ImGui::Combo("Point", &Settings.PointShadowMapSizeLevel, QualityLevelStr, IM_ARRAYSIZE(QualityLevelStr)))
	{
		Settings.PointShadowMapSizeLevel = FMath::Clamp(Settings.PointShadowMapSizeLevel, 0, IM_ARRAYSIZE(QualityLevelStr) - 1);
	}

	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	if (ImGui::Combo("Spot", &Settings.SpotShadowMapSizeLevel, QualityLevelStr, IM_ARRAYSIZE(QualityLevelStr)))
	{
		Settings.SpotShadowMapSizeLevel = FMath::Clamp(Settings.SpotShadowMapSizeLevel, 0, IM_ARRAYSIZE(QualityLevelStr) - 1);
	}

	const char* PcfModes[] = { "Off", "8 (Samples)", "16 (Samples)", "32 (Samples)", "PCSS" };
	int PcfModeIndex = static_cast<int>(Settings.PcfMode);
	ImGui::SetNextItemWidth(EditorUIScale::Px(200.0f));
	if (ImGui::Combo("Shadow Filter", &PcfModeIndex, PcfModes, IM_ARRAYSIZE(PcfModes)))
		Settings.PcfMode = static_cast<EPcfMode>(PcfModeIndex);
}

