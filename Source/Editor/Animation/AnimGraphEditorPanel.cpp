#include "EnginePCH.h"
#include "Editor/Animation/AnimGraphEditorPanel.h"
#include "Editor/HitoriEd/EditorEngine.h"
#include "Editor/EditorUI/EditorUIScale.h"
#include "Editor/Animation/AnimationEditorSplitter.h"
#include "Asset/AssetManager.h"
#include "Asset/BlendSpace1D.h"
#include "Asset/BlendSpace2D.h"
#include "Asset/FbxImporter/FbxPath.h"
#include "Render/SkeletalMesh.h"
#include "Render/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor/SkyLightActor.h"
#include "GameFramework/Actor/DirectionalLightActor.h"
#include "Components/SkyLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/BillboardComponent.h"
#include "Animation/AnimGraphInstance.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace
{
	constexpr const char* PoseNames[] = { "Sequence Player", "BlendSpace 1D Player", "BlendSpace 2D Player" };
	constexpr const char* InputNames[] = { "Constant", "Speed", "Direction", "Jump Requested", "Falling", "Grounded", "Moving On Ground", "Sequence Ending" };
	constexpr const char* CompareNames[] = { "Always", ">", ">=", "<", "<=" };
	bool Inside(ImVec2 P, ImVec2 Min, ImVec2 Max)
	{
		return P.x >= Min.x && P.y >= Min.y && P.x <= Max.x && P.y <= Max.y;
	}
	FString BlendSpaceFilePath(const FString& AssetPath)
	{
		const fs::path File = FbxPath::FromString(AssetPath);
		return FbxPath::ToString(File.is_absolute() || AssetPath.starts_with("Assets/")
			? File : fs::path("Assets") / File);
	}
}

void FAnimGraphEditorPanel::OpenAsset(const FString& InPath)
{
	FAnimGraphAsset Loaded;
	FString Error;
	if (!FAnimGraphAsset::Load(InPath, Loaded, Error)) { Status = Error; SetOpen(true); return; }
	Path = InPath; Graph = std::move(Loaded); Status.clear(); bDirty = bPreviewRefreshRequested = false;
	strncpy_s(AssetName, FbxPath::FromString(Path).stem().string().c_str(), _TRUNCATE);
	SelectedId = Graph.EntryState; SelectedPoseNode = 1; View = EView::Root; Pan = { 0, 0 }; Zoom = 1.0f;
	SetOpen(true);
	StartPreview();
}

void FAnimGraphEditorPanel::NewAsset(const FString& InPath, const FString& InSkeletonKey)
{
	// 스켈레톤은 호출자가 고른다. 로드할 수 없으면 만들지 않는다.
	USkeleton* Skeleton = UAssetManager::LoadSkeleton(InSkeletonKey);
	if (!Skeleton) { Status = "Cannot load skeleton: " + InSkeletonKey; SetOpen(true); return; }
	FAnimGraphAsset NewGraph;
	NewGraph.SkeletonPath = Skeleton->GetPath();
	FAnimGraphState Initial;
	Initial.Id = 1; Initial.Name = "Idle"; Initial.X = 80.0f; Initial.Y = 120.0f;
	// 초기 상태에는 이 스켈레톤의 시퀀스 중 경로순 첫 번째를 넣는다. 없으면 비워 두고 사용자가 고른다.
	TArray<UAnimSequence*> Sequences;
	UAssetManager::GetAnimSequencesForSkeleton(Skeleton, Sequences);
	std::sort(Sequences.begin(), Sequences.end(),
		[](const UAnimSequence* A, const UAnimSequence* B) { return A->GetPath() < B->GetPath(); });
	if (!Sequences.IsEmpty()) Initial.AssetPath = Sequences[0]->GetPath();
	NewGraph.EntryState = Initial.Id;
	NewGraph.States.push_back(Initial);
	Path = InPath; Graph = std::move(NewGraph); SelectedId = 1; SelectedPoseNode = 1; View = EView::Root;
	strncpy_s(AssetName, FbxPath::FromString(Path).stem().string().c_str(), _TRUNCATE);
	bDirty = true; bPreviewRefreshRequested = false; Status = "Choose a sequence, then save."; Pan = { 0, 0 }; Zoom = 1.0f;
	SetOpen(true);
	FString Error;
	if (Graph.Validate(Error)) Save();
	else StartPreview();
}

void FAnimGraphEditorPanel::Save()
{
	const FString Name = AssetName;
	if (Name.empty() || Name == "." || Name == ".." || Name.find_first_of("\\/:*?\"<>|") != FString::npos)
	{ Status = "Invalid AnimGraph name."; return; }
	const fs::path Target = FbxPath::FromString(Path).parent_path() / (Name + ".animgraph");
	const FString TargetPath = FbxPath::ToString(Target);
	if (TargetPath != Path && fs::exists(Target))
	{ Status = "An asset with this name already exists."; return; }
	if (Graph.Save(TargetPath, Status)) { Path = TargetPath; bDirty = false; Status = "Saved"; StartPreview(); }
}

void FAnimGraphEditorPanel::Release()
{
	Client.SetWorld(nullptr);
	PreviewCharacter = nullptr;
	if (PreviewWorld)
	{
		PreviewWorld->ClearWorld();
		PreviewWorld->FlushPendingDestruction();
		if (GEngine) GEngine->DestroyWorld(PreviewWorld);
		PreviewWorld = nullptr;
	}
}

void FAnimGraphEditorPanel::StartPreview()
{
	Release();
	if (Graph.SkeletonPath.empty()) return;
	USkeleton* Skeleton = UAssetManager::LoadSkeleton(Graph.SkeletonPath);
	TArray<USkeletalMesh*> Meshes;
	UAssetManager::GetSkeletalMeshesForSkeleton(Skeleton, Meshes);
	if (Meshes.IsEmpty() && Skeleton)
	{
		fs::path SkeletonFile;
		FString Error;
		if (FbxPath::ResolveKey(Graph.SkeletonPath, SkeletonFile, Error))
		{
			const fs::path Directory = SkeletonFile.parent_path();
			std::error_code Ec;
			for (fs::directory_iterator It(Directory, Ec), End; !Ec && It != End; It.increment(Ec))
			{
				if (!It->is_regular_file() || It->path().extension() != ".fbxbin" ||
					!It->path().filename().string().starts_with("SK_")) continue;
				UAssetManager::LoadFbxSkeletalMesh(FbxPath::ToString(It->path()));
				UAssetManager::GetSkeletalMeshesForSkeleton(Skeleton, Meshes);
				if (!Meshes.IsEmpty()) break;
			}
		}
	}
	if (Meshes.IsEmpty()) return;
	PreviewWorld = UWorld::CreateWorld(EWorldType::Editor, false);
	if (!PreviewWorld) return;
	PreviewCharacter = PreviewWorld->SpawnActor<ACharacter>();
	if (!PreviewCharacter) { Release(); return; }
	if (auto* Sky = PreviewWorld->SpawnActor<ASkyLightActor>())
	{
		Sky->GetBillboardComponent()->SetVisibility(false);
		Sky->GetLightComponent()->SetIntensity(0.35f);
	}
	if (auto* Sun = PreviewWorld->SpawnActor<ADirectionalLightActor>())
	{
		Sun->GetBillboardComponent()->SetVisibility(false);
		Sun->GetArrowComponent()->SetVisibility(false);
		Sun->GetLightComponent()->SetRelativeRotation(FRotator(40.0f, 160.0f, 0.0f));
		Sun->GetLightComponent()->SetIntensity(2.0f);
	}
	auto* MeshComponent = PreviewCharacter->GetSkeletalMeshComponent();
	MeshComponent->SetSkeletalMesh(Meshes[0]);
	MeshComponent->SetAnimInstanceClass(UAnimGraphInstance::StaticClass());
	RefreshPreviewGraph();
	bPreviewRefreshRequested = false;
	Client.SetWorld(PreviewWorld);
	Client.SetViewportType(ELevelViewportType::Perspective);
	Client.Resize(500, 400);
	const FBox Bounds = MeshComponent->CalcBounds();
	const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
	const float Radius = std::max(0.5f, (Bounds.Max - Bounds.Min).Size() * 0.5f);
	Client.SetViewRotation(FRotator(10.0f, 165.0f, 0.0f));
	Client.SetViewLocation(Center - Client.GetViewForward() * (Radius * 2.5f));
}

void FAnimGraphEditorPanel::RefreshPreviewGraph()
{
	if (!PreviewCharacter) { if (!Graph.SkeletonPath.empty()) StartPreview(); return; }
	FString Error;
	if (!Graph.Validate(Error)) { Status = "Preview: " + Error; return; }
	if (auto* Instance = Cast<UAnimGraphInstance>(PreviewCharacter->GetSkeletalMeshComponent()->GetAnimInstance()))
	{
		if (!Instance->SetPreviewGraph(Graph)) Status = "Preview: animation asset or skeleton is incompatible.";
		else if (Status.starts_with("Preview:")) Status.clear();
	}
	else Status = "Preview: animation instance is missing.";
}

void FAnimGraphEditorPanel::Tick(float DeltaTime)
{
	if (!IsOpen())
	{
		bFocused = false;
		// 창을 완전히 닫으면 프리뷰 월드를 해제한다. 그래프 데이터는 유지하고, 다시 열면 OnRender에서 프리뷰를 복원한다.
		if (PreviewWorld)
		{
			Release();
			bRestorePreviewOnOpen = true;
		}
		return;
	}
	// 도킹된 탭이 가려져 있으면 재생하지 않는다. 월드는 그대로 둔다.
	if (!bVisible)
	{
		return;
	}
	if (!PreviewWorld || !PreviewCharacter || !bPreviewPlaying) return;
	if (auto* Movement = PreviewCharacter->GetCharacterMovement())
	{
		const float Radians = FMath::DegreesToRadians(Graph.bInvertDirection ? -PreviewDirection : PreviewDirection);
		const FQuat Facing = PreviewCharacter->GetActorQuat();
		Movement->Velocity = (Facing.GetForwardVector() * std::cos(Radians) +
			Facing.GetRightVector() * std::sin(Radians)) * (PreviewSpeed / 100.0f);
	}
	if (auto* Instance = Cast<UAnimGraphInstance>(PreviewCharacter->GetSkeletalMeshComponent()->GetAnimInstance()))
		Instance->SetPreviewFalling(bPreviewFalling);
	PreviewWorld->Tick(DeltaTime);
	PreviewCharacter->GetSkeletalMeshComponent()->TickComponent(DeltaTime);
}

void FAnimGraphEditorPanel::DrawPreview()
{
	ImGui::TextUnformatted("Preview");
	if (PreviewWorld && Client.GetColorTarget())
	{
		const ImVec2 Size(ImGui::GetContentRegionAvail().x,
			std::max(EditorUIScale::Px(80), ImGui::GetContentRegionAvail().y - EditorUIScale::Px(145)));
		Client.Resize(static_cast<uint32>(std::max(1.0f, Size.x)), static_cast<uint32>(Size.y));
		ImGui::Image(Client.GetColorTarget()->GetSRV(), Size);
		const bool bHovered = ImGui::IsItemHovered();
		const bool bCaptured = bHovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
		float Wheel = ImGui::GetIO().MouseWheel;
		// 우클릭으로 카메라를 잡은 동안 휠은 이동 속도를 바꾼다.
		if (bCaptured && Wheel != 0.0f)
		{
			CameraSpeed = std::clamp(CameraSpeed + Wheel * 0.5f, 0.1f, 20.0f);
			Wheel = 0.0f;
		}
		Client.TickInputFromImGui(ImGui::GetIO().DeltaTime, bCaptured, bHovered,
			static_cast<int32>(Wheel * 120), 4.0f * CameraSpeed, 0.2f);
	}
	else if (Graph.SkeletonPath.empty()) ImGui::TextWrapped("Select a skeleton to start the preview.");
	else ImGui::TextWrapped("No skeletal mesh found for the selected skeleton.");
	ImGui::Checkbox("Play", &bPreviewPlaying);
	ImGui::DragFloat("Speed", &PreviewSpeed, 1.0f, 0.0f, 600.0f);
	ImGui::DragFloat("Direction", &PreviewDirection, 1.0f, -180.0f, 180.0f);
	if (ImGui::Button("Jump") && PreviewCharacter)
	{
		bPreviewFalling = true;
		if (auto* Instance = Cast<UAnimGraphInstance>(PreviewCharacter->GetSkeletalMeshComponent()->GetAnimInstance()))
			Instance->RequestJump();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Falling", &bPreviewFalling);
	if (PreviewCharacter)
		if (auto* Instance = Cast<UAnimGraphInstance>(PreviewCharacter->GetSkeletalMeshComponent()->GetAnimInstance()))
			ImGui::Text("Active State: %d", Instance->GetActiveStateId());
	if (PreviewCharacter && !PreviewCharacter->GetSkeletalMeshComponent()->GetAnimInstance())
		ImGui::TextUnformatted("Preview animation instance is missing.");
	ImGui::Separator();
}

void FAnimGraphEditorPanel::RenderPreview(UEditorEngine* Engine)
{
	if (Engine && IsOpen() && bVisible && PreviewWorld)
		Engine->RenderPreviewViewport(&Client, PreviewWorld, nullptr, nullptr, true);
}

int32 FAnimGraphEditorPanel::NextStateId() const
{
	int32 Id = 1;
	for (const auto& State : Graph.States) Id = std::max(Id, State.Id + 1);
	return Id;
}

int32 FAnimGraphEditorPanel::NextTransitionId() const
{
	int32 Id = 1;
	for (const auto& Transition : Graph.Transitions) Id = std::max(Id, Transition.Id + 1);
	return Id;
}

FAnimGraphState* FAnimGraphEditorPanel::SelectedState()
{
	for (auto& State : Graph.States) if (State.Id == SelectedId) return &State;
	return nullptr;
}

FString FAnimGraphEditorPanel::StateName(int32 Id) const
{
	for (const auto& State : Graph.States) if (State.Id == Id) return State.Name;
	return "Unknown State";
}

FAnimGraphTransition* FAnimGraphEditorPanel::SelectedTransition()
{
	for (auto& Transition : Graph.Transitions) if (Transition.Id == SelectedId) return &Transition;
	return nullptr;
}

void FAnimGraphEditorPanel::AddState()
{
	FAnimGraphState State;
	State.Id = NextStateId(); State.Name = "State " + std::to_string(State.Id);
	State.X = 100.0f + static_cast<float>(Graph.States.size() % 4) * 200.0f;
	State.Y = 100.0f + static_cast<float>(Graph.States.size() / 4) * 120.0f;
	Graph.States.push_back(State); SelectedId = State.Id; bDirty = bPreviewRefreshRequested = true;
}

void FAnimGraphEditorPanel::DrawTree()
{
	if (ImGui::Selectable("AnimGraph", View == EView::Root)) View = EView::Root;
	if (ImGui::TreeNodeEx("State Machine", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Selectable("Main States", View == EView::Machine)) View = EView::Machine;
		for (const auto& State : Graph.States)
		{
			ImGui::PushID(State.Id);
			if (ImGui::Selectable(State.Name.c_str(), View == EView::State && SelectedId == State.Id))
			{ SelectedId = State.Id; SelectedPoseNode = 1; View = EView::State; }
			ImGui::PopID();
		}
		for (const auto& Transition : Graph.Transitions)
		{
			const FString Label = "Transition " + StateName(Transition.From) + " -> " + StateName(Transition.To);
			ImGui::PushID(Transition.Id + 10000);
			if (ImGui::Selectable(Label.c_str(), View == EView::Transition && SelectedId == Transition.Id))
			{ SelectedId = Transition.Id; View = EView::Transition; }
			ImGui::PopID();
		}
		ImGui::TreePop();
	}
	ImGui::Separator();
	ImGui::TextUnformatted("Parameters");
	ImGui::BulletText("Speed");
	ImGui::BulletText("Direction");
}

void FAnimGraphEditorPanel::DrawRoot(ImDrawList* Draw, ImVec2 Origin, ImVec2 Size)
{
	const float Scale = EditorUIScale::Px(1.0f);
	const ImVec2 Machine(Origin.x + Size.x * 0.30f, Origin.y + Size.y * 0.45f);
	const ImVec2 Output(Origin.x + Size.x * 0.68f, Origin.y + Size.y * 0.45f);
	const ImVec2 MachinePin(Machine.x + 180 * Scale, Machine.y + 34 * Scale);
	const ImVec2 OutputPin(Output.x, Output.y + 34 * Scale);
	const ImVec2 Mouse = ImGui::GetMousePos();
	const bool Click = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const bool Release = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
	Draw->AddRectFilled(Machine, { Machine.x + 180 * Scale, Machine.y + 68 * Scale }, IM_COL32(49, 57, 75, 255), 6.0f);
	Draw->AddText({ Machine.x + 12 * Scale, Machine.y + 24 * Scale }, IM_COL32_WHITE, "State Machine");
	Draw->AddRectFilled(Output, { Output.x + 160 * Scale, Output.y + 68 * Scale }, IM_COL32(64, 55, 47, 255), 6.0f);
	Draw->AddText({ Output.x + 12 * Scale, Output.y + 24 * Scale }, IM_COL32_WHITE, "Output Pose");
	Draw->AddCircleFilled(MachinePin, 6 * Scale, IM_COL32(215, 215, 215, 255));
	Draw->AddCircleFilled(OutputPin, 6 * Scale, IM_COL32(215, 215, 215, 255));
	if (Graph.bRootPoseConnected)
		Draw->AddBezierCubic(MachinePin, { MachinePin.x + 50 * Scale, MachinePin.y },
			{ OutputPin.x - 50 * Scale, OutputPin.y }, OutputPin, IM_COL32(210, 210, 210, 255), 2.0f);
	if (Click && Inside(Mouse, { MachinePin.x - 10 * Scale, MachinePin.y - 10 * Scale },
		{ MachinePin.x + 10 * Scale, MachinePin.y + 10 * Scale })) bRootPoseLinkDragging = true;
	else if (Click && Inside(Mouse, { OutputPin.x - 10 * Scale, OutputPin.y - 10 * Scale },
		{ OutputPin.x + 10 * Scale, OutputPin.y + 10 * Scale }))
	{ Graph.bRootPoseConnected = false; bDirty = bPreviewRefreshRequested = true; }
	else if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
		Inside(Mouse, Machine, { Machine.x + 180 * Scale, Machine.y + 68 * Scale })) View = EView::Machine;
	if (bRootPoseLinkDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		Draw->AddLine(MachinePin, Mouse, IM_COL32(255, 220, 140, 255), 2.0f);
	if (Release && bRootPoseLinkDragging)
	{
		if (Inside(Mouse, { OutputPin.x - 14 * Scale, OutputPin.y - 14 * Scale },
			{ OutputPin.x + 14 * Scale, OutputPin.y + 14 * Scale }))
		{ Graph.bRootPoseConnected = true; bDirty = bPreviewRefreshRequested = true; }
		bRootPoseLinkDragging = false;
	}
}

void FAnimGraphEditorPanel::DrawMachine(ImDrawList* Draw, ImVec2 Origin, ImVec2 Size)
{
	const auto ToScreen = [&](float X, float Y) { return ImVec2(Origin.x + Pan.x + X * Zoom, Origin.y + Pan.y + Y * Zoom); };
	const ImVec2 Mouse = ImGui::GetMousePos();
	const bool Click = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const bool Double = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) DraggingState = 0;
	const auto* PreviewInstance = PreviewCharacter
		? Cast<UAnimGraphInstance>(PreviewCharacter->GetSkeletalMeshComponent()->GetAnimInstance()) : nullptr;
	const int32 ActiveState = PreviewInstance ? PreviewInstance->GetActiveStateId() : 0;
	const int32 TargetState = PreviewInstance ? PreviewInstance->GetTargetStateId() : 0;
	if (PendingFrom && bLinkDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		for (const auto& State : Graph.States) if (State.Id == PendingFrom)
		{
			const ImVec2 Start = ToScreen(State.X + 160.0f, State.Y + 32.0f);
			Draw->AddLine(Start, Mouse, IM_COL32(120, 210, 130, 255), 2.0f);
			break;
		}
	if (PendingFrom && bLinkDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		for (const auto& State : Graph.States)
		{
			const ImVec2 Min = ToScreen(State.X, State.Y);
			if (State.Id == PendingFrom || !Inside(Mouse, Min, { Min.x + 160 * Zoom, Min.y + 64 * Zoom })) continue;
			Graph.Transitions.push_back({ NextTransitionId(), PendingFrom, State.Id });
			SelectedId = Graph.Transitions.back().Id; View = EView::Transition; bDirty = bPreviewRefreshRequested = true;
			break;
		}
		PendingFrom = 0; bLinkDragging = false;
	}
	std::set<std::pair<int32, int32>> DrawnPairs;
	for (const auto& Transition : Graph.Transitions)
	{
		if (!DrawnPairs.insert({ Transition.From, Transition.To }).second) continue;
		std::vector<const FAnimGraphTransition*> Rules;
		for (const auto& Rule : Graph.Transitions)
			if (Rule.From == Transition.From && Rule.To == Transition.To) Rules.push_back(&Rule);
		const FAnimGraphState* From = nullptr; const FAnimGraphState* To = nullptr;
		for (const auto& State : Graph.States)
		{
			if (State.Id == Transition.From) From = &State;
			if (State.Id == Transition.To) To = &State;
		}
		if (!From || !To) continue;
		const float DX = To->X - From->X;
		const float DY = To->Y - From->Y;
		const bool Horizontal = std::abs(DX) >= std::abs(DY);
		const float Lane = (Transition.From < Transition.To ? -14.0f : 14.0f) * Zoom;
		ImVec2 A, B, C1, C2;
		if (Horizontal)
		{
			const bool Right = DX >= 0.0f;
			A = ToScreen(From->X + (Right ? 160.0f : 0.0f), From->Y + 32.0f);
			B = ToScreen(To->X + (Right ? 0.0f : 160.0f), To->Y + 32.0f);
			A.y += Lane; B.y += Lane;
			const float Handle = std::max(45.0f * Zoom, std::abs(B.x - A.x) * 0.4f);
			C1 = { A.x + (Right ? Handle : -Handle), A.y };
			C2 = { B.x - (Right ? Handle : -Handle), B.y };
		}
		else
		{
			const bool Down = DY >= 0.0f;
			A = ToScreen(From->X + 80.0f, From->Y + (Down ? 64.0f : 0.0f));
			B = ToScreen(To->X + 80.0f, To->Y + (Down ? 0.0f : 64.0f));
			A.x += Lane; B.x += Lane;
			const float Handle = std::max(45.0f * Zoom, std::abs(B.y - A.y) * 0.4f);
			C1 = { A.x, A.y + (Down ? Handle : -Handle) };
			C2 = { B.x, B.y - (Down ? Handle : -Handle) };
		}
		const bool ActiveTransition = Transition.From == ActiveState && Transition.To == TargetState;
		const bool SelectedTransition = View == EView::Transition &&
			std::any_of(Rules.begin(), Rules.end(), [&](const auto* Rule) { return Rule->Id == SelectedId; });
		const ImU32 Color = ActiveTransition ? IM_COL32(85, 220, 110, 255) :
			SelectedTransition ? IM_COL32(255, 190, 90, 255) : IM_COL32(175, 180, 188, 255);
		Draw->AddBezierCubic(A, C1, C2, B, Color, 2.0f * Zoom);
		const float TangentX = B.x - C2.x;
		const float TangentY = B.y - C2.y;
		const float TangentLength = std::max(1.0f, std::sqrt(TangentX * TangentX + TangentY * TangentY));
		const float ArrowX = TangentX / TangentLength;
		const float ArrowY = TangentY / TangentLength;
		const float ArrowSize = 9.0f * Zoom;
		Draw->AddTriangleFilled(B,
			{ B.x - ArrowX * ArrowSize - ArrowY * ArrowSize * 0.55f, B.y - ArrowY * ArrowSize + ArrowX * ArrowSize * 0.55f },
			{ B.x - ArrowX * ArrowSize + ArrowY * ArrowSize * 0.55f, B.y - ArrowY * ArrowSize - ArrowX * ArrowSize * 0.55f }, Color);
		const ImVec2 Mid((A.x + 3.0f * C1.x + 3.0f * C2.x + B.x) * 0.125f,
			(A.y + 3.0f * C1.y + 3.0f * C2.y + B.y) * 0.125f);
		Draw->AddCircleFilled(Mid, 11.0f * Zoom, Color);
		Draw->AddText({ Mid.x - 5.0f * Zoom, Mid.y - 8.0f * Zoom }, IM_COL32(30, 32, 36, 255), ">");
		ImGui::PushID(Transition.Id);
		if (Click && Inside(Mouse, { Mid.x - 13 * Zoom, Mid.y - 13 * Zoom }, { Mid.x + 13 * Zoom, Mid.y + 13 * Zoom }))
		{
			if (Rules.size() == 1) { SelectedId = Transition.Id; View = EView::Transition; }
			else ImGui::OpenPopup("Transition Rules");
		}
		if (ImGui::IsItemHovered() && Inside(Mouse, { Mid.x - 13 * Zoom, Mid.y - 13 * Zoom },
			{ Mid.x + 13 * Zoom, Mid.y + 13 * Zoom }))
			ImGui::SetTooltip("%s -> %s (%d rule%s)", From->Name.c_str(), To->Name.c_str(),
				static_cast<int>(Rules.size()), Rules.size() == 1 ? "" : "s");
		if (ImGui::BeginPopup("Transition Rules"))
		{
			for (const auto* Rule : Rules)
			{
				ImGui::PushID(Rule->Id);
				const FString Label = FString(InputNames[static_cast<int>(Rule->Input)]) + " " +
					CompareNames[static_cast<int>(Rule->Compare)] + " " + std::to_string(Rule->Threshold);
				if (ImGui::Selectable(Label.c_str())) { SelectedId = Rule->Id; View = EView::Transition; }
				ImGui::PopID();
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	for (const auto& State : Graph.States) if (State.Id == Graph.EntryState)
	{
		const ImVec2 Entry = ToScreen(State.X - 120.0f, State.Y + 16.0f);
		Draw->AddRectFilled(Entry, { Entry.x + 68 * Zoom, Entry.y + 32 * Zoom }, IM_COL32(72, 75, 82, 255), 5.0f);
		Draw->AddText({ Entry.x + 12 * Zoom, Entry.y + 8 * Zoom }, IM_COL32_WHITE, "Entry");
		Draw->AddLine({ Entry.x + 68 * Zoom, Entry.y + 16 * Zoom }, ToScreen(State.X, State.Y + 32.0f),
			IM_COL32(180, 180, 190, 255), 2.0f);
		break;
	}
	for (auto& State : Graph.States)
	{
		const ImVec2 Min = ToScreen(State.X, State.Y);
		const ImVec2 Max(Min.x + 160.0f * Zoom, Min.y + 64.0f * Zoom);
		const bool Selected = SelectedId == State.Id && View != EView::Transition;
		const bool Hovered = ImGui::IsItemHovered() && Inside(Mouse, Min, Max);
		Draw->AddRectFilled(Min, Max, IM_COL32(43, 47, 53, 255), 6.0f);
		Draw->AddRect(Min, Max, State.Id == ActiveState ? IM_COL32(85, 220, 110, 255) :
			Selected ? IM_COL32(240, 170, 80, 255) : IM_COL32(120, 125, 135, 255), 6.0f, 2.0f, ImDrawFlags_None);
		Draw->AddCircleFilled({ Min.x + 16 * Zoom, Min.y + 20 * Zoom }, 6 * Zoom, IM_COL32(180, 186, 195, 255));
		Draw->AddText({ Min.x + 30 * Zoom, Min.y + 10 * Zoom }, IM_COL32_WHITE, State.Name.c_str());
		if (State.Id == Graph.EntryState) Draw->AddText({ Min.x + 30 * Zoom, Min.y + 36 * Zoom }, IM_COL32(120, 210, 130, 255), "Entry State");
		const ImVec2 Pin(Max.x, (Min.y + Max.y) * 0.5f);
		if (Hovered || Selected || PendingFrom == State.Id)
			Draw->AddCircleFilled(Pin, 6 * Zoom, IM_COL32(120, 200, 135, 255));
		if (Click && Inside(Mouse, { Pin.x - 10, Pin.y - 10 }, { Pin.x + 10, Pin.y + 10 }))
		{ PendingFrom = State.Id; bLinkDragging = true; SelectedId = State.Id; View = EView::Machine; }
		else if (Click && Inside(Mouse, Min, Max))
		{
			if (PendingFrom && PendingFrom != State.Id)
			{
				Graph.Transitions.push_back({ NextTransitionId(), PendingFrom, State.Id });
				SelectedId = Graph.Transitions.back().Id; View = EView::Transition; PendingFrom = 0; bDirty = bPreviewRefreshRequested = true;
			}
			else { SelectedId = State.Id; View = EView::Machine; DraggingState = State.Id; }
		}
		if (Double && Inside(Mouse, Min, Max)) { SelectedId = State.Id; SelectedPoseNode = 1; View = EView::State; PendingFrom = 0; }
		if (View == EView::Machine && DraggingState == State.Id && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{ State.X += ImGui::GetIO().MouseDelta.x / Zoom; State.Y += ImGui::GetIO().MouseDelta.y / Zoom; bDirty = true; }
		if (Hovered) ImGui::SetTooltip("Double-click to edit pose; drag the right pin to add a transition");
	}
	Draw->AddText({ Origin.x + 12, Origin.y + 12 }, IM_COL32(145, 150, 160, 255),
		"States: double-click to edit pose  |  Arrows: click to edit transition  |  Drag a state pin to connect");
	if (PendingFrom) Draw->AddText({ Origin.x + 12, Origin.y + Size.y - 28 }, IM_COL32(255, 210, 110, 255), "Click a destination state (Esc to cancel)");
}

void FAnimGraphEditorPanel::DrawState(ImDrawList* Draw, ImVec2 Origin, ImVec2 Size)
{
	FAnimGraphState* State = SelectedState();
	if (!State) return;
	const float Scale = EditorUIScale::Px(1.0f);
	const ImVec2 Player(Origin.x + Size.x * 0.35f + State->PlayerOffsetX * Scale,
		Origin.y + Size.y * 0.38f + State->PlayerOffsetY * Scale);
	const ImVec2 Output(Origin.x + Size.x * 0.73f + State->OutputOffsetX * Scale,
		Origin.y + Size.y * 0.41f + State->OutputOffsetY * Scale);
	const ImVec2 PlayerPosePin(Player.x + 185 * Scale, Player.y + 60 * Scale);
	const ImVec2 OutputPosePin(Output.x, Output.y + 29 * Scale);
	ImVec2 ActivePosePin = PlayerPosePin;
	ImVec2 DraggingPosePin = PlayerPosePin;
	if (!State->bPoseConnected)
		Draw->AddText({ Origin.x + 16, Origin.y + 16 }, IM_COL32(255, 185, 95, 255),
			"Output Pose disconnected - drag from the player output to reconnect");
	const auto ParameterPos = [&](int Parameter)
	{
		return ImVec2(Origin.x + Size.x * 0.35f - 205 * Scale +
			(Parameter == 1 ? State->SpeedOffsetX : State->DirectionOffsetX) * Scale,
			Origin.y + Size.y * 0.38f + (50 + (Parameter - 1) * 48) * Scale +
			(Parameter == 1 ? State->SpeedOffsetY : State->DirectionOffsetY) * Scale);
	};
	Draw->AddRectFilled(Player, { Player.x + 185 * Scale, Player.y + 125 * Scale }, IM_COL32(39, 55, 43, 255), 6);
	if (SelectedPoseNode == 1) Draw->AddRect(Player, { Player.x + 185 * Scale, Player.y + 125 * Scale }, IM_COL32(240, 170, 80, 255), 6.0f, 2.0f, ImDrawFlags_None);
	Draw->AddText({ Player.x + 10, Player.y + 10 }, IM_COL32_WHITE, PoseNames[static_cast<int>(State->Pose)]);
	Draw->AddText({ Player.x + 10, Player.y + 37 }, IM_COL32(180, 190, 180, 255), State->AssetPath.c_str());
	if (OnOpenBlendSpace && State->Pose != EAnimGraphPose::Sequence && !State->AssetPath.empty() &&
		ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
		Inside(ImGui::GetMousePos(), Player, { Player.x + 185 * Scale, Player.y + 125 * Scale }))
		OnOpenBlendSpace(BlendSpaceFilePath(State->AssetPath));
	const int Inputs = State->Pose == EAnimGraphPose::Sequence ? 0 : State->Pose == EAnimGraphPose::BlendSpace1D ? 1 : 2;
	const ImVec2 Mouse = ImGui::GetMousePos();
	const bool Click = ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const bool Release = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
	if (Click && Inside(Mouse, { PlayerPosePin.x - 10 * Scale, PlayerPosePin.y - 10 * Scale },
		{ PlayerPosePin.x + 10 * Scale, PlayerPosePin.y + 10 * Scale }))
	{ bPoseLinkDragging = true; PendingPosePlayerId = 0; DraggingPoseNode = 0; }
	else if (Click && Inside(Mouse, { OutputPosePin.x - 11 * Scale, OutputPosePin.y - 11 * Scale },
		{ OutputPosePin.x + 11 * Scale, OutputPosePin.y + 11 * Scale }))
	{ State->bPoseConnected = false; bDirty = bPreviewRefreshRequested = true; DraggingPoseNode = 0; }
	else if (Click && Inside(Mouse, Player, { Player.x + 185 * Scale, Player.y + 125 * Scale }))
	{ DraggingPoseNode = 1; SelectedPoseNode = 1; }
	else if (Click && Inside(Mouse, Output, { Output.x + 140 * Scale, Output.y + 58 * Scale }))
	{ DraggingPoseNode = 2; SelectedPoseNode = 2; }
	if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && DraggingPoseNode)
	{
		const float DX = ImGui::GetIO().MouseDelta.x / Scale;
		const float DY = ImGui::GetIO().MouseDelta.y / Scale;
		if (DraggingPoseNode == 1) { State->PlayerOffsetX += DX; State->PlayerOffsetY += DY; }
		if (DraggingPoseNode == 2) { State->OutputOffsetX += DX; State->OutputOffsetY += DY; }
		if (DraggingPoseNode == 3) { State->SpeedOffsetX += DX; State->SpeedOffsetY += DY; }
		if (DraggingPoseNode == 4) { State->DirectionOffsetX += DX; State->DirectionOffsetY += DY; }
		bDirty = true;
	}
	const bool HasBlendPlayer = Inputs > 0 || std::any_of(State->Players.begin(), State->Players.end(),
		[](const auto& Node) { return Node.Pose != EAnimGraphPose::Sequence; });
	for (int Parameter = 1; Parameter <= 2 && HasBlendPlayer; ++Parameter)
	{
		const ImVec2 Min = ParameterPos(Parameter);
		const ImVec2 Max(Min.x + 125 * Scale, Min.y + 30 * Scale);
		Draw->AddRectFilled(Min, Max, IM_COL32(42, 77, 47, 255), 12);
		if (SelectedPoseNode == Parameter + 2) Draw->AddRect(Min, Max, IM_COL32(240, 170, 80, 255), 12.0f, 2.0f, ImDrawFlags_None);
		Draw->AddText({ Min.x + 10, Min.y + 7 }, IM_COL32_WHITE, InputNames[Parameter]);
		Draw->AddCircleFilled({ Max.x, (Min.y + Max.y) * 0.5f }, 5 * Scale, IM_COL32(85, 215, 95, 255));
		const ImVec2 Pin(Max.x, (Min.y + Max.y) * 0.5f);
		if (Click && Inside(Mouse, { Pin.x - 10 * Scale, Pin.y - 10 * Scale },
			{ Pin.x + 10 * Scale, Pin.y + 10 * Scale }))
		{ PendingParameter = Parameter; DraggingPoseNode = 0; }
		else if (Click && Inside(Mouse, Min, Max)) { DraggingPoseNode = Parameter + 2; SelectedPoseNode = Parameter + 2; }
		if (PendingParameter == Parameter && ImGui::IsMouseDown(ImGuiMouseButton_Left))
			Draw->AddLine({ Max.x, (Min.y + Max.y) * 0.5f }, Mouse, IM_COL32(85, 215, 95, 255), 2);
	}
	for (int I = 0; I < Inputs; ++I)
	{
		const float Y = Player.y + (70 + I * 26) * Scale;
		const EAnimGraphInput Input = I == 0 ? State->InputX : State->InputY;
		Draw->AddCircleFilled({ Player.x, Y }, 5 * Scale, IM_COL32(85, 215, 95, 255));
		Draw->AddText({ Player.x + 9, Y - 7 }, IM_COL32_WHITE, I == 0 ? "X" : "Y");
		if (Click && Inside(Mouse, { Player.x - 10 * Scale, Y - 10 * Scale },
			{ Player.x + 10 * Scale, Y + 10 * Scale }))
		{
			if (I == 0) State->InputX = EAnimGraphInput::Constant;
			else State->InputY = EAnimGraphInput::Constant;
			bDirty = bPreviewRefreshRequested = true;
		}
		if (Input != EAnimGraphInput::Constant)
		{
			const ImVec2 Parameter = ParameterPos(static_cast<int>(Input));
			const ImVec2 Source(Parameter.x + 125 * Scale, Parameter.y + 15 * Scale);
			Draw->AddBezierCubic(Source, { Source.x + 30 * Scale, Source.y },
				{ Player.x - 30 * Scale, Y }, { Player.x, Y }, IM_COL32(85, 215, 95, 255), 2);
		}
		if (Release && PendingParameter > 0 && Inside(Mouse, { Player.x - 12 * Scale, Y - 12 * Scale },
			{ Player.x + 12 * Scale, Y + 12 * Scale }))
		{
			if (I == 0) State->InputX = static_cast<EAnimGraphInput>(PendingParameter);
			else State->InputY = static_cast<EAnimGraphInput>(PendingParameter);
			bDirty = bPreviewRefreshRequested = true;
		}
	}
	for (auto& Node : State->Players)
	{
		const ImVec2 Position(Origin.x + Node.X * Scale, Origin.y + Node.Y * Scale);
		const ImVec2 Max(Position.x + 185 * Scale, Position.y + 125 * Scale);
		const ImVec2 PosePin(Position.x + 185 * Scale, Position.y + 60 * Scale);
		const int32 VisualId = Node.Id + 1000;
		const int NodeInputs = Node.Pose == EAnimGraphPose::Sequence ? 0 :
			Node.Pose == EAnimGraphPose::BlendSpace1D ? 1 : 2;
		Draw->AddRectFilled(Position, Max, IM_COL32(39, 55, 43, 255), 6.0f);
		if (SelectedPoseNode == VisualId)
			Draw->AddRect(Position, Max, IM_COL32(240, 170, 80, 255), 6.0f, 2.0f, ImDrawFlags_None);
		Draw->AddText({ Position.x + 10, Position.y + 10 }, IM_COL32_WHITE, PoseNames[static_cast<int>(Node.Pose)]);
		Draw->AddText({ Position.x + 10, Position.y + 37 }, IM_COL32(180, 190, 180, 255), Node.AssetPath.c_str());
		if (OnOpenBlendSpace && Node.Pose != EAnimGraphPose::Sequence && !Node.AssetPath.empty() &&
			ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
			Inside(ImGui::GetMousePos(), Position, Max))
			OnOpenBlendSpace(BlendSpaceFilePath(Node.AssetPath));
		Draw->AddCircleFilled(PosePin, 6 * Scale, IM_COL32(215, 215, 215, 255));
		if (State->OutputPlayerId == Node.Id) ActivePosePin = PosePin;
		if (PendingPosePlayerId == Node.Id) DraggingPosePin = PosePin;
		if (Click && Inside(Mouse, { PosePin.x - 10 * Scale, PosePin.y - 10 * Scale },
			{ PosePin.x + 10 * Scale, PosePin.y + 10 * Scale }))
		{ bPoseLinkDragging = true; PendingPosePlayerId = Node.Id; DraggingPoseNode = 0; }
		else if (Click && Inside(Mouse, Position, Max))
		{ DraggingPoseNode = VisualId; SelectedPoseNode = VisualId; }
		if (DraggingPoseNode == VisualId && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{ Node.X += ImGui::GetIO().MouseDelta.x / Scale; Node.Y += ImGui::GetIO().MouseDelta.y / Scale; bDirty = true; }
		for (int I = 0; I < NodeInputs; ++I)
		{
			const float Y = Position.y + (70 + I * 26) * Scale;
			const EAnimGraphInput Input = I == 0 ? Node.InputX : Node.InputY;
			Draw->AddCircleFilled({ Position.x, Y }, 5 * Scale, IM_COL32(85, 215, 95, 255));
			Draw->AddText({ Position.x + 9, Y - 7 }, IM_COL32_WHITE, I == 0 ? "X" : "Y");
			if (Click && Inside(Mouse, { Position.x - 10 * Scale, Y - 10 * Scale },
				{ Position.x + 10 * Scale, Y + 10 * Scale }))
			{ (I == 0 ? Node.InputX : Node.InputY) = EAnimGraphInput::Constant; bDirty = bPreviewRefreshRequested = true; }
			if (Input != EAnimGraphInput::Constant)
			{
				const ImVec2 Parameter = ParameterPos(static_cast<int>(Input));
				const ImVec2 Source(Parameter.x + 125 * Scale, Parameter.y + 15 * Scale);
				Draw->AddBezierCubic(Source, { Source.x + 30 * Scale, Source.y },
					{ Position.x - 30 * Scale, Y }, { Position.x, Y }, IM_COL32(85, 215, 95, 255), 2.0f);
			}
			if (Release && PendingParameter > 0 && Inside(Mouse, { Position.x - 12 * Scale, Y - 12 * Scale },
				{ Position.x + 12 * Scale, Y + 12 * Scale }))
			{ (I == 0 ? Node.InputX : Node.InputY) = static_cast<EAnimGraphInput>(PendingParameter); bDirty = bPreviewRefreshRequested = true; }
		}
	}
	if (Release) PendingParameter = -1;
	Draw->AddRectFilled(Output, { Output.x + 140 * Scale, Output.y + 58 * Scale }, IM_COL32(64, 55, 47, 255), 6);
	if (SelectedPoseNode == 2) Draw->AddRect(Output, { Output.x + 140 * Scale, Output.y + 58 * Scale }, IM_COL32(240, 170, 80, 255), 6.0f, 2.0f, ImDrawFlags_None);
	Draw->AddText({ Output.x + 10, Output.y + 10 }, IM_COL32_WHITE, "Output Pose");
	Draw->AddCircleFilled(PlayerPosePin, 6 * Scale, IM_COL32(215, 215, 215, 255));
	Draw->AddCircleFilled(OutputPosePin, 6 * Scale, IM_COL32(215, 215, 215, 255));
	if (State->bPoseConnected)
		Draw->AddBezierCubic(ActivePosePin, { ActivePosePin.x + 45 * Scale, ActivePosePin.y },
			{ OutputPosePin.x - 45 * Scale, OutputPosePin.y }, OutputPosePin, IM_COL32(210, 210, 210, 255), 2);
	if (bPoseLinkDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		Draw->AddLine(DraggingPosePin, Mouse, IM_COL32(255, 220, 140, 255), 2);
	if (Release && bPoseLinkDragging)
	{
		if (Inside(Mouse, { OutputPosePin.x - 14 * Scale, OutputPosePin.y - 14 * Scale },
			{ OutputPosePin.x + 14 * Scale, OutputPosePin.y + 14 * Scale }))
		{ State->bPoseConnected = true; State->OutputPlayerId = PendingPosePlayerId; bDirty = bPreviewRefreshRequested = true; }
		bPoseLinkDragging = false;
	}
	if (Release) DraggingPoseNode = 0;
	if (ImGui::IsItemHovered() && Inside(Mouse, { OutputPosePin.x - 11 * Scale, OutputPosePin.y - 11 * Scale },
		{ OutputPosePin.x + 11 * Scale, OutputPosePin.y + 11 * Scale }))
		ImGui::SetTooltip("Click to disconnect; drag from the player output to reconnect");
}

void FAnimGraphEditorPanel::DrawTransition(ImDrawList* Draw, ImVec2 Origin, ImVec2 Size)
{
	const auto* Transition = SelectedTransition();
	if (!Transition) return;
	const ImVec2 A(Origin.x + Size.x * 0.18f, Origin.y + Size.y * 0.47f);
	const ImVec2 C(Origin.x + Size.x * 0.43f, Origin.y + Size.y * 0.47f);
	const ImVec2 B(Origin.x + Size.x * 0.75f, Origin.y + Size.y * 0.47f);
	Draw->AddRectFilled(A, { A.x + 145, A.y + 55 }, IM_COL32(42, 77, 47, 255), 6);
	Draw->AddText({ A.x + 10, A.y + 18 }, IM_COL32_WHITE, InputNames[static_cast<int>(Transition->Input)]);
	Draw->AddRectFilled(C, { C.x + 160, C.y + 55 }, IM_COL32(42, 52, 68, 255), 6);
	const FString Rule = FString(CompareNames[static_cast<int>(Transition->Compare)]) + " " + std::to_string(Transition->Threshold);
	Draw->AddText({ C.x + 10, C.y + 18 }, IM_COL32_WHITE, Rule.c_str());
	Draw->AddRectFilled(B, { B.x + 160, B.y + 55 }, IM_COL32(64, 55, 47, 255), 6);
	Draw->AddText({ B.x + 10, B.y + 18 }, IM_COL32_WHITE, "Can Enter Transition");
	Draw->AddLine({ A.x + 145, A.y + 27 }, { C.x, C.y + 27 }, IM_COL32(100, 200, 110, 255), 2);
	Draw->AddBezierCubic({ C.x + 160, C.y + 27 }, { C.x + 210, C.y + 27 },
		{ B.x - 65, B.y + 27 }, { B.x, B.y + 27 }, IM_COL32(100, 200, 110, 255), 2);
}

void FAnimGraphEditorPanel::DrawCanvas()
{
	ImGui::TextUnformatted(View == EView::Root ? "AnimGraph" :
		View == EView::Machine ? "AnimGraph > State Machine" :
		View == EView::State ? "AnimGraph > State" : "AnimGraph > Transition Rule");
	ImGui::SameLine();
	if (View == EView::Machine && ImGui::SmallButton("Back to AnimGraph")) View = EView::Root;
	else if ((View == EView::State || View == EView::Transition) && ImGui::SmallButton("Back to State Machine")) View = EView::Machine;
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##AnimGraphCanvas", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	ImDrawList* Draw = ImGui::GetWindowDrawList();
	Draw->PushClipRect(Origin, { Origin.x + Size.x, Origin.y + Size.y }, true);
	Draw->AddRectFilled(Origin, { Origin.x + Size.x, Origin.y + Size.y }, IM_COL32(27, 29, 33, 255));
	const float Grid = 60.0f * Zoom;
	for (float X = std::fmod(Pan.x, Grid); X < Size.x; X += Grid)
		Draw->AddLine({ Origin.x + X, Origin.y }, { Origin.x + X, Origin.y + Size.y }, IM_COL32(42, 44, 49, 255));
	for (float Y = std::fmod(Pan.y, Grid); Y < Size.y; Y += Grid)
		Draw->AddLine({ Origin.x, Origin.y + Y }, { Origin.x + Size.x, Origin.y + Y }, IM_COL32(42, 44, 49, 255));
	if (View == EView::Root) DrawRoot(Draw, Origin, Size);
	else if (View == EView::Machine) DrawMachine(Draw, Origin, Size);
	else if (View == EView::State) DrawState(Draw, Origin, Size);
	else DrawTransition(Draw, Origin, Size);
	Draw->PopClipRect();
	// 우클릭 드래그로 팬한 뒤 버튼을 뗄 때는 컨텍스트 메뉴를 열지 않는다.
	const ImVec2 RightDrag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
	const bool bRightPanned = ImGui::IsMouseReleased(ImGuiMouseButton_Right) && (RightDrag.x != 0.0f || RightDrag.y != 0.0f);
	if (ImGui::IsItemHovered())
	{
		// 휠 클릭 드래그 또는 우클릭 드래그로 캔버스를 이동한다.
		if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))
		{ Pan.x += ImGui::GetIO().MouseDelta.x; Pan.y += ImGui::GetIO().MouseDelta.y; }
		if (ImGui::GetIO().MouseWheel != 0 && View == EView::Machine)
			Zoom = std::clamp(Zoom + ImGui::GetIO().MouseWheel * 0.1f, 0.5f, 2.0f);
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) PendingFrom = 0;
	}
	if (!bRightPanned && View == EView::Machine && ImGui::BeginPopupContextItem("##AnimGraphContext"))
	{
		if (ImGui::MenuItem("Add State")) AddState();
		ImGui::EndPopup();
	}
	if (!bRightPanned && View == EView::State && ImGui::BeginPopupContextItem("##StatePoseContext"))
	{
		FAnimGraphState* State = SelectedState();
		if (State)
		{
			const ImVec2 Spawn = ImGui::GetMousePosOnOpeningCurrentPopup();
			for (int Type = 0; Type < 3; ++Type)
				if (ImGui::MenuItem(PoseNames[Type]))
				{
					FAnimGraphPlayerNode Node;
					for (const auto& Existing : State->Players) Node.Id = std::max(Node.Id, Existing.Id);
					++Node.Id;
					Node.Pose = static_cast<EAnimGraphPose>(Type);
					const float Scale = EditorUIScale::Px(1.0f);
					Node.X = (Spawn.x - Origin.x) / Scale;
					Node.Y = (Spawn.y - Origin.y) / Scale;
					State->Players.push_back(std::move(Node));
					SelectedPoseNode = State->Players.back().Id + 1000;
					bDirty = bPreviewRefreshRequested = true;
				}
		}
		ImGui::EndPopup();
	}
}

void FAnimGraphEditorPanel::DrawDetails()
{
	if (View != EView::Transition)
	{
		if (ImGui::BeginCombo("Skeleton", Graph.SkeletonPath.empty() ? "Select skeleton" : Graph.SkeletonPath.c_str()))
		{
			TArray<UAnimSequence*> Sequences;
			UAssetManager::GetAllAnimSequences(Sequences);
			std::set<FString> Seen;
			for (auto* Sequence : Sequences)
			{
				if (!Sequence || !Sequence->Skeleton) continue;
				const FString Candidate = Sequence->Skeleton->GetPath();
				if (!Seen.insert(Candidate).second) continue;
				if (ImGui::Selectable(Candidate.c_str(), Candidate == Graph.SkeletonPath) && Candidate != Graph.SkeletonPath)
				{
					Graph.SkeletonPath = Candidate;
					for (auto& State : Graph.States)
					{
						State.AssetPath.clear();
						for (auto& Node : State.Players) Node.AssetPath.clear();
					}
					bDirty = true;
					StartPreview();
				}
			}
			ImGui::EndCombo();
		}
	}
	if (View == EView::Transition)
	{
		FAnimGraphTransition* Transition = SelectedTransition();
		if (!Transition) return;
		ImGui::Text("Transition %s -> %s", StateName(Transition->From).c_str(), StateName(Transition->To).c_str());
		int Input = std::max(1, static_cast<int>(Transition->Input)) - 1;
		if (ImGui::Combo("Condition Source", &Input,
			"Speed\0Direction\0Jump Requested\0Falling\0Grounded\0Moving On Ground\0Sequence Ending\0"))
		{ Transition->Input = static_cast<EAnimGraphInput>(Input + 1); bDirty = bPreviewRefreshRequested = true; }
		if (Transition->Input != EAnimGraphInput::SequenceEnding)
		{
			int Compare = static_cast<int>(Transition->Compare);
			if (ImGui::Combo("Compare", &Compare, CompareNames, 5)) { Transition->Compare = static_cast<EAnimGraphCompare>(Compare); bDirty = bPreviewRefreshRequested = true; }
		}
		if (Transition->Compare != EAnimGraphCompare::Always || Transition->Input == EAnimGraphInput::SequenceEnding)
			if (ImGui::DragFloat("Threshold", &Transition->Threshold, 0.1f)) bDirty = bPreviewRefreshRequested = true;
		if (ImGui::DragFloat("Blend Time", &Transition->BlendTime, 0.01f, 0.0f, 10.0f)) bDirty = bPreviewRefreshRequested = true;
		const auto It = std::find_if(Graph.Transitions.begin(), Graph.Transitions.end(),
			[Id = Transition->Id](const auto& Item) { return Item.Id == Id; });
		if (It != Graph.Transitions.end())
		{
			if (It != Graph.Transitions.begin() && ImGui::Button("Higher Priority"))
			{ std::iter_swap(It, It - 1); bDirty = bPreviewRefreshRequested = true; }
			if (It + 1 != Graph.Transitions.end() && ImGui::Button("Lower Priority"))
			{ std::iter_swap(It, It + 1); bDirty = bPreviewRefreshRequested = true; }
		}
		if (ImGui::Button("Delete Transition"))
		{
			const int32 Id = Transition->Id;
			std::erase_if(Graph.Transitions, [Id](const auto& Item) { return Item.Id == Id; });
			SelectedId = Graph.EntryState; View = EView::Machine; bDirty = bPreviewRefreshRequested = true;
		}
		return;
	}
	if (View == EView::Root)
	{
		if (ImGui::Checkbox("Output Pose Connected", &Graph.bRootPoseConnected)) bDirty = bPreviewRefreshRequested = true;
		if (ImGui::Checkbox("Invert Direction", &Graph.bInvertDirection)) bDirty = bPreviewRefreshRequested = true;
		return;
	}
	FAnimGraphState* State = SelectedState();
	if (!State) return;
	char Name[128]{};
	strncpy_s(Name, State->Name.c_str(), sizeof(Name) - 1);
	if (ImGui::InputText("State Name", Name, sizeof(Name))) { State->Name = Name; bDirty = true; }
	if (ImGui::Checkbox("Output Pose Connected", &State->bPoseConnected)) bDirty = bPreviewRefreshRequested = true;
	if (ImGui::Button("Set as Entry")) { Graph.EntryState = State->Id; bDirty = bPreviewRefreshRequested = true; }
	FAnimGraphPlayerNode* Extra = nullptr;
	for (auto& Node : State->Players)
		if (SelectedPoseNode == Node.Id + 1000) { Extra = &Node; break; }
	EAnimGraphPose& PoseType = Extra ? Extra->Pose : State->Pose;
	FString& AssetPath = Extra ? Extra->AssetPath : State->AssetPath;
	EAnimGraphInput& InputX = Extra ? Extra->InputX : State->InputX;
	EAnimGraphInput& InputY = Extra ? Extra->InputY : State->InputY;
	float& ConstantX = Extra ? Extra->ConstantX : State->ConstantX;
	float& ConstantY = Extra ? Extra->ConstantY : State->ConstantY;
	bool& bLoop = Extra ? Extra->bLoop : State->bLoop;
	float& PlayRate = Extra ? Extra->PlayRate : State->PlayRate;
	if (Extra && ImGui::Button("Delete Player Node"))
	{
		const int32 Id = Extra->Id;
		std::erase_if(State->Players, [Id](const auto& Node) { return Node.Id == Id; });
		if (State->OutputPlayerId == Id) { State->bPoseConnected = false; State->OutputPlayerId = 0; }
		SelectedPoseNode = 1; bDirty = bPreviewRefreshRequested = true;
		return;
	}
	int Pose = static_cast<int>(PoseType);
	if (ImGui::Combo("Player", &Pose, PoseNames, 3))
	{ PoseType = static_cast<EAnimGraphPose>(Pose); AssetPath.clear(); bDirty = bPreviewRefreshRequested = true; }
	if (PoseType == EAnimGraphPose::Sequence)
	{
		TArray<UAnimSequence*> Sequences;
		UAssetManager::GetAllAnimSequences(Sequences);
		if (ImGui::BeginCombo("Animation", AssetPath.empty() ? "Select sequence" : AssetPath.c_str()))
		{
			for (auto* Sequence : Sequences)
				if (Sequence && Sequence->Skeleton && (Graph.SkeletonPath.empty() || Graph.SkeletonPath == Sequence->Skeleton->GetPath()) &&
					ImGui::Selectable(Sequence->GetPath().c_str(), AssetPath == Sequence->GetPath()))
				{ AssetPath = Sequence->GetPath(); Graph.SkeletonPath = Sequence->Skeleton->GetPath(); bDirty = bPreviewRefreshRequested = true; }
			ImGui::EndCombo();
		}
	}
	else
	{
		if (ImGui::BeginCombo("BlendSpace", AssetPath.empty() ? "Select BlendSpace" : AssetPath.c_str()))
		{
			std::error_code Ec;
			for (fs::recursive_directory_iterator It("Assets", Ec), End; !Ec && It != End; It.increment(Ec))
			{
				if (It->path().extension() != ".blendspace") continue;
				const FString Candidate = FbxPath::ToString(It->path());
				UBlendSpace* Blend = UAssetManager::LoadBlendSpace(Candidate);
				if (!Blend || !Blend->Skeleton || (!Graph.SkeletonPath.empty() && Graph.SkeletonPath != Blend->Skeleton->GetPath())) continue;
				if (PoseType == EAnimGraphPose::BlendSpace1D && !Blend->IsA<UBlendSpace1D>()) continue;
				if (PoseType == EAnimGraphPose::BlendSpace2D && !Blend->IsA<UBlendSpace2D>()) continue;
				if (ImGui::Selectable(Candidate.c_str(), AssetPath == Candidate))
				{ AssetPath = Candidate; Graph.SkeletonPath = Blend->Skeleton->GetPath(); bDirty = bPreviewRefreshRequested = true; }
			}
			ImGui::EndCombo();
		}
		int X = static_cast<int>(InputX);
		if (ImGui::Combo("X Input", &X, InputNames, 3)) { InputX = static_cast<EAnimGraphInput>(X); bDirty = bPreviewRefreshRequested = true; }
		if (InputX == EAnimGraphInput::Constant && ImGui::DragFloat("X Value", &ConstantX, 0.1f)) bDirty = bPreviewRefreshRequested = true;
		if (PoseType == EAnimGraphPose::BlendSpace2D)
		{
			int Y = static_cast<int>(InputY);
			if (ImGui::Combo("Y Input", &Y, InputNames, 3)) { InputY = static_cast<EAnimGraphInput>(Y); bDirty = bPreviewRefreshRequested = true; }
			if (InputY == EAnimGraphInput::Constant && ImGui::DragFloat("Y Value", &ConstantY, 0.1f)) bDirty = bPreviewRefreshRequested = true;
		}
	}
	if (ImGui::Checkbox("Loop", &bLoop)) bDirty = bPreviewRefreshRequested = true;
	if (ImGui::DragFloat("Play Rate", &PlayRate, 0.01f, 0.01f, 10.0f)) bDirty = bPreviewRefreshRequested = true;
	if (State->Id != Graph.EntryState && ImGui::Button("Delete State"))
	{
		const int32 Id = State->Id;
		std::erase_if(Graph.States, [Id](const auto& Item) { return Item.Id == Id; });
		std::erase_if(Graph.Transitions, [Id](const auto& Item) { return Item.From == Id || Item.To == Id; });
		SelectedId = Graph.EntryState; View = EView::Machine; bDirty = bPreviewRefreshRequested = true;
	}
}

void FAnimGraphEditorPanel::DrawAssetBrowser()
{
	ImGui::TextUnformatted("Animation Assets");
	FAnimGraphState* State = View == EView::Transition ? nullptr : SelectedState();
	if (!State) { ImGui::TextWrapped("Select a state to assign an asset."); return; }
	FAnimGraphPlayerNode* Extra = nullptr;
	if (View == EView::State)
		for (auto& Node : State->Players)
			if (SelectedPoseNode == Node.Id + 1000) { Extra = &Node; break; }
	EAnimGraphPose& PoseType = Extra ? Extra->Pose : State->Pose;
	FString& AssetPath = Extra ? Extra->AssetPath : State->AssetPath;
	TArray<UAnimSequence*> Sequences;
	UAssetManager::GetAllAnimSequences(Sequences);
	if (ImGui::CollapsingHeader("Sequences", ImGuiTreeNodeFlags_DefaultOpen))
		for (auto* Sequence : Sequences)
		{
			if (!Sequence || !Sequence->Skeleton ||
				(!Graph.SkeletonPath.empty() && Graph.SkeletonPath != Sequence->Skeleton->GetPath())) continue;
			if (ImGui::Selectable(Sequence->GetPath().c_str(), AssetPath == Sequence->GetPath()))
			{
				PoseType = EAnimGraphPose::Sequence;
				AssetPath = Sequence->GetPath();
				Graph.SkeletonPath = Sequence->Skeleton->GetPath();
				bDirty = bPreviewRefreshRequested = true;
			}
		}
	if (ImGui::CollapsingHeader("BlendSpaces"))
	{
		std::error_code Ec;
		for (fs::recursive_directory_iterator It("Assets", Ec), End; !Ec && It != End; It.increment(Ec))
		{
			if (It->path().extension() != ".blendspace") continue;
			const FString Candidate = FbxPath::ToString(It->path());
			UBlendSpace* Blend = UAssetManager::LoadBlendSpace(Candidate);
			if (!Blend || !Blend->Skeleton ||
				(!Graph.SkeletonPath.empty() && Graph.SkeletonPath != Blend->Skeleton->GetPath())) continue;
			if (ImGui::Selectable(Candidate.c_str(), AssetPath == Candidate))
			{
				PoseType = Blend->IsA<UBlendSpace2D>() ? EAnimGraphPose::BlendSpace2D : EAnimGraphPose::BlendSpace1D;
				AssetPath = Candidate;
				Graph.SkeletonPath = Blend->Skeleton->GetPath();
				bDirty = bPreviewRefreshRequested = true;
			}
		}
	}
}

void FAnimGraphEditorPanel::OnRender()
{
	if (bRestorePreviewOnOpen)
	{
		bRestorePreviewOnOpen = false;
		StartPreview();
	}
	ImGui::SetNextWindowSize(ImVec2(EditorUIScale::Px(1250), EditorUIScale::Px(750)), ImGuiCond_FirstUseEver);
	bVisible = BeginPanel();
	bFocused = bVisible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	// Ctrl+S: 이 창에 포커스가 있을 때만 저장한다. Ctrl+Shift+S는 전체 저장이라 여기서 받지 않는다.
	if (bFocused && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_S, false))
	{
		Save();
	}
	if (!bVisible) { ImGui::End(); return; }
	ImGui::SetNextItemWidth(EditorUIScale::Px(180));
	if (ImGui::InputText("AnimGraph Name", AssetName, sizeof(AssetName))) bDirty = true;
	ImGui::SameLine();
	if (ImGui::Button("Save")) Save(); ImGui::SameLine();
	if (ImGui::Button("Validate")) Graph.Validate(Status);
	// SameLine은 바로 다음 아이템에만 적용된다. 더티가 아닐 때 이후의 Graph Tree 차일드가 이 줄에 붙지 않도록 조건부로 호출한다.
	if (bDirty) { ImGui::SameLine(); ImGui::TextUnformatted("*"); }
	if (!Status.empty()) ImGui::TextWrapped("%s", Status.c_str());
	const ImVec2 Available = ImGui::GetContentRegionAvail();
	const float Split = EditorUIScale::Px(6);
	const float Left = Available.x * LeftRatio;
	const float Right = Available.x * RightRatio;
	ImGui::BeginChild("Graph Tree", { Left, Available.y }, ImGuiChildFlags_Borders);
	const float TreeHeight = ImGui::GetContentRegionAvail().y;
	ImGui::BeginChild("Preview", { 0, TreeHeight * PreviewRatio }, ImGuiChildFlags_Borders);
	DrawPreview(); ImGui::EndChild();
	PreviewRatio = std::clamp(PreviewRatio + AnimationEditorSplitter::Draw("##PreviewTreeSplit", false,
		ImGui::GetContentRegionAvail().x) / std::max(1.0f, TreeHeight), 0.20f, 0.80f);
	ImGui::BeginChild("Tree", { 0, 0 });
	DrawTree(); ImGui::EndChild();
	ImGui::EndChild(); ImGui::SameLine(0, 0);
	LeftRatio = std::clamp(LeftRatio + AnimationEditorSplitter::Draw("##GraphLeftSplit", true, Available.y) /
		std::max(1.0f, Available.x), 0.10f, 0.40f);
	ImGui::SameLine(0, 0);
	ImGui::BeginChild("Graph", { std::max(1.0f, Available.x - Left - Right - Split * 2), Available.y }, ImGuiChildFlags_Borders);
	DrawCanvas(); ImGui::EndChild(); ImGui::SameLine(0, 0);
	RightRatio = std::clamp(RightRatio - AnimationEditorSplitter::Draw("##GraphRightSplit", true, Available.y) /
		std::max(1.0f, Available.x), 0.10f, 0.40f);
	ImGui::SameLine(0, 0);
	ImGui::BeginChild("Right", { 0, Available.y });
	const float RightHeight = ImGui::GetContentRegionAvail().y;
	ImGui::BeginChild("Details", { 0, RightHeight * DetailsRatio }, ImGuiChildFlags_Borders);
	DrawDetails(); ImGui::EndChild();
	DetailsRatio = std::clamp(DetailsRatio + AnimationEditorSplitter::Draw("##GraphDetailsSplit", false,
		ImGui::GetContentRegionAvail().x) / std::max(1.0f, RightHeight), 0.20f, 0.80f);
	ImGui::BeginChild("Asset Browser", { 0, 0 }, ImGuiChildFlags_Borders);
	DrawAssetBrowser(); ImGui::EndChild();
	ImGui::EndChild();
	if (bPreviewRefreshRequested) { RefreshPreviewGraph(); bPreviewRefreshRequested = false; }
	ImGui::End();
}
