#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Editor/Viewports/EditorViewportClient.h"
#include "Asset/AnimGraphAsset.h"
#include <functional>

class UWorld;
class ACharacter;
class UEditorEngine;

class FAnimGraphEditorPanel : public IEditorPanel
{
public:
	bool Init() override { SetOpen(false); return true; }
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override { return "AnimGraph Editor"; }
	bool IsRootPanel() const override { return true; }
	void OpenAsset(const FString& InPath);
	void OpenMontageAsset(const FString& InPath);
	void NewMontageAsset(const FString& InPath);
	void OpenAimOffsetAsset(const FString& InPath);
	void NewAimOffsetAsset(const FString& InPath);
	void SetOpenBlendSpaceCallback(std::function<void(const FString&)> Callback) { OnOpenBlendSpace = std::move(Callback); }
	void SetOpenMontageCallback(std::function<void(const FString&)> Callback) { OnOpenMontage = std::move(Callback); }
	void SetOpenAimOffsetCallback(std::function<void(const FString&)> Callback) { OnOpenAimOffset = std::move(Callback); }
	void NotifyAnimationAssetSaved() { bPreviewRefreshRequested = true; }
	void NewAsset(const FString& InPath);
	void RenderPreview(UEditorEngine* Engine);
	void Release();
	~FAnimGraphEditorPanel() override { Release(); }

private:
	enum class EView { Root, Machine, State, Transition };
	void DrawTree();
	void DrawCanvas();
	void DrawRoot(ImDrawList* Draw, ImVec2 Origin, ImVec2 Size);
	void DrawMachine(ImDrawList* Draw, ImVec2 Origin, ImVec2 Size);
	void DrawState(ImDrawList* Draw, ImVec2 Origin, ImVec2 Size);
	void DrawTransition(ImDrawList* Draw, ImVec2 Origin, ImVec2 Size);
	void DrawDetails();
	void DrawAssetBrowser();
	void DrawMontageEditorWindow();
	void DrawAimOffsetEditorWindow();
	void AddState();
	int32 NextRootNodeId() const;
	FRootAnimGraphNode* SelectedRootNode();
	FRootAnimGraphNode* FindRootNode(int32 Id);
	FAnimGraphState* SelectedState();
	FAnimGraphTransition* SelectedTransition();
	FString StateName(int32 Id) const;
	int32 NextStateId() const;
	int32 NextTransitionId() const;
	void Save();
	void StartPreview();
	void RefreshPreviewGraph();
	void DrawPreview();

	FString Path;
	std::function<void(const FString&)> OnOpenBlendSpace;
	std::function<void(const FString&)> OnOpenMontage;
	std::function<void(const FString&)> OnOpenAimOffset;
	float LeftRatio = 0.20f;
	float RightRatio = 0.24f;
	float PreviewRatio = 0.54f;
	float DetailsRatio = 0.68f;
	char AssetName[128]{};
	FString Status;
	FAnimGraphAsset Graph;
	EView View = EView::Root;
	int32 SelectedId = 0;
	int32 SelectedRootNodeId = 1;
	int32 DraggingRootNodeId = 0;
	int32 PendingRootFromNodeId = 0;
	int32 PendingFrom = 0;
	bool bLinkDragging = false;
	int32 DraggingState = 0;
	int32 PendingParameter = -1;
	bool bPoseLinkDragging = false;
	int32 PendingPosePlayerId = 0;
	bool bRootPoseLinkDragging = false;
	int32 DraggingPoseNode = 0;
	int32 SelectedPoseNode = 0;
	bool bDirty = false;
	bool bPreviewRefreshRequested = false;
	ImVec2 Pan{ 0.0f, 0.0f };
	float Zoom = 1.0f;
	UWorld* PreviewWorld = nullptr;
	ACharacter* PreviewCharacter = nullptr;
	FEditorViewportClient Client;
	float PreviewSpeed = 0.0f;
	float PreviewDirection = 0.0f;
	float PreviewAimPitch = 0.0f;
	bool bPreviewFalling = false;
	bool bPreviewPlaying = true;
	bool bVisible = false;
	int32 PreviewFireCount = 0;
	FString LastPreviewNotify;

	bool bMontageEditorOpen = false;
	FString MontageEditPath = "Assets/Animations/RifleFire.animmontage";
	FAnimMontageAsset EditingMontage;
	FString MontageStatus;
	float MontageScrubTime = 0.0f;

	bool bAimOffsetEditorOpen = false;
	FString AimOffsetEditPath = "Assets/Animations/RifleAimOffset.aimoffset";
	FAimOffsetAsset EditingAimOffset;
	FString AimOffsetStatus;
};
