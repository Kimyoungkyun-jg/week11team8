#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Editor/Gizmo/Gizmo.h"

class UWorld;
class USkeletalMeshComponent;
class UTexture2D;
class FSkyboxRenderer;

enum class EBoneDrawMode : uint8
{
	AllHierarchy,       // 모든 계층구조
	SelectedOnly,       // 선택된 본만
	SelectedAndParents, // 선택된 본과 부모
	None                // 없음
};

// FBX 하나를 띄우는 창. 레벨과 섞이지 않도록 월드를 따로 갖는다.
struct FSkeletalMeshViewer
{
	~FSkeletalMeshViewer();

	FString Path;
	uint32 Id = 0;
	UWorld* World = nullptr;
	USkeletalMeshComponent* MeshComponent = nullptr;
	FEditorViewportClient Client;
	bool bOpen = true;
	bool bVisible = false;

	TArray<TArray<uint32>> Children;
	TArray<int32> WeightedVertexCounts;
	int32 SelectedBone = -1;

	FGizmo Gizmo;
	USceneComponent* BoneGizmoTarget = nullptr;

	ImVec2 ViewSize{ 0.0f, 0.0f };

	bool bScrollTreeToSelection = false;

	EBoneDrawMode BoneDrawMode = EBoneDrawMode::AllHierarchy;
	bool bShowMesh = true;
	bool bShowGrid = false;
	float DetailsHeight = 280.0f;
	float CameraSpeed = 4.0f;
	bool bEditingCameraSpeed = false;
	bool bFocusCameraSpeedInput = false;
	float SaveNotificationTimer = 0.0f;
	UTexture2D* ShowFlagsIcon = nullptr;
	TUniquePtr<FSkyboxRenderer> Skybox;
};

class FSkeletalMeshViewerPanel : public IEditorPanel
{
public:
	bool Init() override { return true; }
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "Skeletal Mesh Viewer"; }
	bool IsRootPanel() const override { return true; }
	void OnHidden() override { bHasFocusedViewer = false; }
	void OpenFBX(const FString& Path);
	void Release();
	const TArray<TUniquePtr<FSkeletalMeshViewer>>& GetViewers() const { return Viewers; }
	bool HasFocuesdViewer() const { return IsOpen() && bHasFocusedViewer; }

private:
	void DrawViewport(FSkeletalMeshViewer& Viewer);
	void DrawViewportToolbar(FSkeletalMeshViewer& Viewer);
	void DrawBoneTree(FSkeletalMeshViewer& Viewer);
	void DrawBoneNode(FSkeletalMeshViewer& Viewer, int32 BoneIndex);
	void DrawBoneDetails(FSkeletalMeshViewer& Viewer);
	void SaveViewerPose(FSkeletalMeshViewer& Viewer);
	int32 DrawSkeleton(FSkeletalMeshViewer& Viewer, const ImVec2& ViewMin, const ImVec2& ViewSize);
	void SelectBone(FSkeletalMeshViewer& Viewer, int32 BoneIndex);
	void UpdateBoneGizmo(FSkeletalMeshViewer& Viewer, const ImVec2& ViewMin, bool bLeftDown);

	TArray<TUniquePtr<FSkeletalMeshViewer>> Viewers;
	uint32 NextId = 0;
	bool bHasFocusedViewer = false;
};
