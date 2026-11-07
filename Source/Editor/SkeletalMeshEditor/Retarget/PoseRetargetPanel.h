#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"

class UWorld;
class USkeletalMesh;
class USkeletalMeshComponent;
class FLineBatcher;

class FPoseRetargetPanel : public IEditorPanel
{
public:
	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Pose Retarget"; }
	bool IsRootPanel() const override { return true; }
	const char* GetOpenStateSettingsName() const override { return "PoseRetarget"; }
	void Release();
	UWorld* GetWorld(int32 Index) const { return Index >= 0 && Index < 3 ? PreviewWorlds[Index] : nullptr; }
	FEditorViewportClient& GetViewport(int32 Index) { return Viewports[Index]; }
	bool IsViewportVisible(int32 Index) const { return FocusMode == 0 || FocusMode == Index + 1; }
	void DrawBones(int32 Index, FLineBatcher& Lines, const FMatrix& ViewProjection) const;

private:
	void SetRig(int32 Index, USkeletalMesh* Mesh);
	void DrawRigPicker(int32 Index);
	void RefreshFbxFiles();
	void DrawViewport(int32 Index, const char* Label);
	void DrawMapping();
	void FitCamera(int32 Index);

	UWorld* PreviewWorlds[3]{};
	USkeletalMeshComponent* Components[3]{};
	FEditorViewportClient Viewports[3];
	USkeletalMesh* SourceRig = nullptr;
	USkeletalMesh* TargetMesh = nullptr;
	TArray<int32> TargetToSource;
	TArray<int32> TargetAimChild;
	TArray<FString> AvailableFbxPaths;
	char ProfileName[128] = "DragonToHuman";
	FString Message;
	int32 SourceYaw = 0;
	float CameraSpeed = 1.0f;
	float MappingPanelHeight = 320.0f;
	int32 FocusMode = 0;
	bool bResultPoseApplied = false;
	bool bShowBones = true;
	bool bShowMesh = true;
	bool bShowMapping = true;
};
