#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Containers/Array.h"
#include "Render/SkyboxRenderer.h"

class UAnimSequence;
class UAnimSingleNodeInstance;
class USkeletalMesh;
class UWorld;
class UEditorEngine;
class ASkyLightActor;
class ADirectionalLightActor;
class USkeletalMeshDebugComponent;

class FAnimSequenceViewerPanel : public IEditorPanel
{
public:
	FAnimSequenceViewerPanel();
	~FAnimSequenceViewerPanel() override;
	bool Init() override { SetOpen(false); return true; }
	const char* GetPanelName() const override { return "Animation Sequence"; }
	bool IsRootPanel() const override { return true; }
	void OpenSequence(const FString& Path);
	void Tick(float DeltaTime) override;
	void OnRender() override;
	void RenderPreview(UEditorEngine* Engine);
	void Release();

private:
	void SetPreviewMesh(USkeletalMesh* Mesh);
	void DrawBoneTree(int32 Parent);
	void DrawBoneDetails();
	void DrawAssetDetails();
	void DrawViewport();
	void DrawTimeline();
	void DrawAssetBrowser();

	FString Path;
	FString Status;
	UAnimSequence* Sequence = nullptr;
	UWorld* PreviewWorld = nullptr;
	USkeletalMeshDebugComponent* MeshComponent = nullptr;
	ASkyLightActor* SkyLight = nullptr;
	ADirectionalLightActor* DirectionalLight = nullptr;
	FEditorViewportClient Client;
	float CameraSpeed = 1.0f;
	TUniquePtr<FSkyboxRenderer> Skybox;
	TArray<TArray<int32>> BoneChildren;
	int32 SelectedBone = -1;
	float PreviewRate = 1.0f;
	bool bPlaying = true;
	bool bLoop = true;
	bool bShowGrid = true;
	bool bShowSkybox = true;
	float SkyLightIntensity = 0.3f;
	float DirectionalLightIntensity = 1.0f;
	bool bVisible = false;
	char Search[128]{};
};
