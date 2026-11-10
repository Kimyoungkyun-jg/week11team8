#include "EnginePCH.h"
#include "Asset/AnimGraphAsset.h"
#include "Core/ResourcePaths.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace
{
	fs::path ResolvePath(const FString& Path)
	{
		fs::path Relative(Path);
		if (Relative.extension() != ".animgraph" || Relative.is_absolute()) return {};
		if (Relative.begin() != Relative.end() && *Relative.begin() == "Assets") Relative = Relative.lexically_relative("Assets");
		for (const auto& Part : Relative) if (Part == ".." || Part == ".") return {};
		return Relative.empty() ? fs::path{} : fs::path(ContentPaths::AssetRoot) / Relative;
	}

	bool ReadInt(const json& Object, const char* Key, int32& Out)
	{
		if (!Object.contains(Key) || !Object[Key].is_number_integer()) return false;
		Out = Object[Key].get<int32>();
		return true;
	}
	bool ReadFloat(const json& Object, const char* Key, float& Out)
	{
		if (!Object.contains(Key) || !Object[Key].is_number()) return false;
		Out = Object[Key].get<float>();
		return std::isfinite(Out);
	}
}

bool FAnimGraphAsset::Validate(FString& Error) const
{
	if (States.empty()) { Error = "Graph needs at least one state."; return false; }
	if (States.size() > 256 || Transitions.size() > 1024) { Error = "Graph is too large."; return false; }
	std::set<int32> StateIds, TransitionIds;
	for (const auto& State : States)
	{
		if (State.Id <= 0 || !StateIds.insert(State.Id).second || State.Name.empty() ||
			!std::isfinite(State.X) || !std::isfinite(State.Y) || !std::isfinite(State.PlayRate) || State.PlayRate <= 0 ||
			!std::isfinite(State.ConstantX) || !std::isfinite(State.ConstantY) ||
			!std::isfinite(State.PlayerOffsetX) || !std::isfinite(State.PlayerOffsetY) ||
			!std::isfinite(State.OutputOffsetX) || !std::isfinite(State.OutputOffsetY) ||
			!std::isfinite(State.SpeedOffsetX) || !std::isfinite(State.SpeedOffsetY) ||
			!std::isfinite(State.DirectionOffsetX) || !std::isfinite(State.DirectionOffsetY) ||
			static_cast<int>(State.Pose) < 0 || static_cast<int>(State.Pose) > 2 ||
			static_cast<int>(State.InputX) < 0 || static_cast<int>(State.InputX) > 2 ||
			static_cast<int>(State.InputY) < 0 || static_cast<int>(State.InputY) > 2)
		{ Error = "Invalid state."; return false; }
		if (State.AssetPath.empty() && State.OutputPlayerId == 0) { Error = "State has no animation asset: " + State.Name; return false; }
		const FString Extension = State.Pose == EAnimGraphPose::Sequence ? ".animseq" : ".blendspace";
		if (!State.AssetPath.empty() && !State.AssetPath.ends_with(Extension)) { Error = "Wrong asset type in state: " + State.Name; return false; }
		if (State.Players.size() > 256) { Error = "Too many player nodes in state: " + State.Name; return false; }
		std::set<int32> PlayerIds;
		for (const auto& Player : State.Players)
		{
			const FString PlayerExtension = Player.Pose == EAnimGraphPose::Sequence ? ".animseq" : ".blendspace";
			if (Player.Id <= 0 || !PlayerIds.insert(Player.Id).second ||
				static_cast<int>(Player.Pose) < 0 || static_cast<int>(Player.Pose) > 2 ||
				static_cast<int>(Player.InputX) < 0 || static_cast<int>(Player.InputX) > 2 ||
				static_cast<int>(Player.InputY) < 0 || static_cast<int>(Player.InputY) > 2 ||
				!std::isfinite(Player.X) || !std::isfinite(Player.Y) ||
				!std::isfinite(Player.ConstantX) || !std::isfinite(Player.ConstantY) ||
				!std::isfinite(Player.PlayRate) || Player.PlayRate <= 0 ||
				(!Player.AssetPath.empty() && !Player.AssetPath.ends_with(PlayerExtension)))
			{ Error = "Invalid player node in state: " + State.Name; return false; }
		}
		if (State.OutputPlayerId != 0)
		{
			const auto Player = std::find_if(State.Players.begin(), State.Players.end(),
				[&](const auto& Item) { return Item.Id == State.OutputPlayerId; });
			if (Player == State.Players.end() || (State.bPoseConnected && Player->AssetPath.empty()))
			{ Error = "Invalid Output Pose source in state: " + State.Name; return false; }
		}
	}
	if (!StateIds.contains(EntryState)) { Error = "Entry state is missing."; return false; }
	for (const auto& Transition : Transitions)
	{
		if (Transition.Id <= 0 || !TransitionIds.insert(Transition.Id).second ||
			!StateIds.contains(Transition.From) || !StateIds.contains(Transition.To) || Transition.From == Transition.To ||
			static_cast<int>(Transition.Input) < 0 || static_cast<int>(Transition.Input) > 7 ||
			(Transition.Compare != EAnimGraphCompare::Always && Transition.Input == EAnimGraphInput::Constant) ||
			static_cast<int>(Transition.Compare) < 0 || static_cast<int>(Transition.Compare) > 4 ||
			!std::isfinite(Transition.Threshold) || !std::isfinite(Transition.BlendTime) || Transition.BlendTime < 0)
		{ Error = "Invalid transition."; return false; }
		if (Transition.Input == EAnimGraphInput::SequenceEnding)
		{
			const auto From = std::find_if(States.begin(), States.end(),
				[&](const FAnimGraphState& State) { return State.Id == Transition.From; });
			EAnimGraphPose FromPose = EAnimGraphPose::Sequence;
			if (From != States.end())
			{
				FromPose = From->Pose;
				if (From->OutputPlayerId != 0)
				{
					const auto Player = std::find_if(From->Players.begin(), From->Players.end(),
						[&](const auto& Item) { return Item.Id == From->OutputPlayerId; });
					if (Player != From->Players.end()) FromPose = Player->Pose;
				}
			}
			if (Transition.Threshold < 0.0f || From == States.end() || FromPose != EAnimGraphPose::Sequence)
			{ Error = "Sequence Ending requires a sequence state."; return false; }
		}
	}
	Error.clear();
	return true;
}

bool FAnimGraphAsset::Load(const FString& Path, FAnimGraphAsset& Out, FString& Error)
{
	const fs::path File = ResolvePath(Path);
	if (File.empty()) { Error = "Invalid AnimGraph path."; return false; }
	std::ifstream In(File);
	if (!In) { Error = "Cannot open AnimGraph."; return false; }
	json Data = json::parse(In, nullptr, false);
	if (!Data.is_object() || !Data.contains("Version") || Data["Version"] != 1 ||
		!Data.contains("States") || !Data["States"].is_array() ||
		!Data.contains("Transitions") || !Data["Transitions"].is_array() ||
		!Data.contains("Skeleton") || !Data["Skeleton"].is_string())
	{ Error = "Invalid AnimGraph format."; return false; }
	FAnimGraphAsset Loaded;
	Loaded.SkeletonPath = Data["Skeleton"].get<FString>();
	if (Data.contains("RootPoseConnected"))
	{
		if (!Data["RootPoseConnected"].is_boolean()) { Error = "Invalid root pose connection."; return false; }
		Loaded.bRootPoseConnected = Data["RootPoseConnected"].get<bool>();
	}
	if (Data.contains("InvertDirection"))
	{
		if (!Data["InvertDirection"].is_boolean()) { Error = "Invalid direction setting."; return false; }
		Loaded.bInvertDirection = Data["InvertDirection"].get<bool>();
	}
	if (!ReadInt(Data, "EntryState", Loaded.EntryState)) { Error = "Missing entry state."; return false; }
	for (const auto& Item : Data["States"])
	{
		FAnimGraphState State;
		int32 Pose, InputX, InputY;
		if (!Item.is_object() || !ReadInt(Item, "Id", State.Id) || !Item.contains("Name") || !Item["Name"].is_string() ||
			!Item.contains("Asset") || !Item["Asset"].is_string() || !ReadFloat(Item, "X", State.X) ||
			!ReadFloat(Item, "Y", State.Y) || !ReadInt(Item, "Pose", Pose) || !ReadInt(Item, "InputX", InputX) ||
			!ReadInt(Item, "InputY", InputY) || !ReadFloat(Item, "ConstantX", State.ConstantX) ||
			!ReadFloat(Item, "ConstantY", State.ConstantY) || !ReadFloat(Item, "PlayRate", State.PlayRate) ||
			!Item.contains("Loop") || !Item["Loop"].is_boolean())
		{ Error = "Invalid state data."; return false; }
		State.Name = Item["Name"].get<FString>(); State.AssetPath = Item["Asset"].get<FString>();
		State.Pose = static_cast<EAnimGraphPose>(Pose); State.InputX = static_cast<EAnimGraphInput>(InputX);
		State.InputY = static_cast<EAnimGraphInput>(InputY); State.bLoop = Item["Loop"].get<bool>();
		if (Item.contains("PoseConnected"))
		{
			if (!Item["PoseConnected"].is_boolean()) { Error = "Invalid pose connection."; return false; }
			State.bPoseConnected = Item["PoseConnected"].get<bool>();
		}
		if (Item.contains("OutputPlayerId") && !ReadInt(Item, "OutputPlayerId", State.OutputPlayerId))
		{ Error = "Invalid Output Pose source."; return false; }
		if (Item.contains("Players"))
		{
			if (!Item["Players"].is_array()) { Error = "Invalid player nodes."; return false; }
			for (const auto& Node : Item["Players"])
			{
				FAnimGraphPlayerNode Player;
				int32 NodePose, NodeInputX, NodeInputY;
				if (!Node.is_object() || !ReadInt(Node, "Id", Player.Id) ||
					!ReadInt(Node, "Pose", NodePose) || !ReadInt(Node, "InputX", NodeInputX) ||
					!ReadInt(Node, "InputY", NodeInputY) || !ReadFloat(Node, "X", Player.X) ||
					!ReadFloat(Node, "Y", Player.Y) || !ReadFloat(Node, "ConstantX", Player.ConstantX) ||
					!ReadFloat(Node, "ConstantY", Player.ConstantY) || !ReadFloat(Node, "PlayRate", Player.PlayRate) ||
					!Node.contains("Asset") || !Node["Asset"].is_string() ||
					!Node.contains("Loop") || !Node["Loop"].is_boolean())
				{ Error = "Invalid player node data."; return false; }
				Player.Pose = static_cast<EAnimGraphPose>(NodePose);
				Player.InputX = static_cast<EAnimGraphInput>(NodeInputX);
				Player.InputY = static_cast<EAnimGraphInput>(NodeInputY);
				Player.AssetPath = Node["Asset"].get<FString>();
				Player.bLoop = Node["Loop"].get<bool>();
				State.Players.push_back(std::move(Player));
			}
		}
		struct FPositionField { const char* Key; float* Value; };
		const FPositionField Positions[] = {
			{ "PlayerOffsetX", &State.PlayerOffsetX }, { "PlayerOffsetY", &State.PlayerOffsetY },
			{ "OutputOffsetX", &State.OutputOffsetX }, { "OutputOffsetY", &State.OutputOffsetY },
			{ "SpeedOffsetX", &State.SpeedOffsetX }, { "SpeedOffsetY", &State.SpeedOffsetY },
			{ "DirectionOffsetX", &State.DirectionOffsetX }, { "DirectionOffsetY", &State.DirectionOffsetY }
		};
		for (const auto& Position : Positions)
			if (Item.contains(Position.Key) && !ReadFloat(Item, Position.Key, *Position.Value))
			{ Error = "Invalid node position."; return false; }
		Loaded.States.push_back(std::move(State));
	}
	for (const auto& Item : Data["Transitions"])
	{
		FAnimGraphTransition Transition;
		int32 Input, Compare;
		if (!Item.is_object() || !ReadInt(Item, "Id", Transition.Id) || !ReadInt(Item, "From", Transition.From) ||
			!ReadInt(Item, "To", Transition.To) || !ReadInt(Item, "Input", Input) || !ReadInt(Item, "Compare", Compare) ||
			!ReadFloat(Item, "Threshold", Transition.Threshold) || !ReadFloat(Item, "BlendTime", Transition.BlendTime))
		{ Error = "Invalid transition data."; return false; }
		Transition.Input = static_cast<EAnimGraphInput>(Input);
		Transition.Compare = static_cast<EAnimGraphCompare>(Compare);
		Loaded.Transitions.push_back(Transition);
	}
	if (!Loaded.Validate(Error)) return false;
	Out = std::move(Loaded);
	return true;
}

bool FAnimGraphAsset::Save(const FString& Path, FString& Error) const
{
	if (!Validate(Error)) return false;
	const fs::path File = ResolvePath(Path);
	if (File.empty()) { Error = "Invalid AnimGraph path."; return false; }
	json Data = { { "Version", 1 }, { "Skeleton", SkeletonPath }, { "RootPoseConnected", bRootPoseConnected },
		{ "InvertDirection", bInvertDirection }, { "EntryState", EntryState },
		{ "States", json::array() }, { "Transitions", json::array() } };
	for (const auto& State : States)
	{
		json Players = json::array();
		for (const auto& Player : State.Players)
			Players.push_back({ { "Id", Player.Id }, { "Pose", static_cast<int>(Player.Pose) },
				{ "Asset", Player.AssetPath }, { "InputX", static_cast<int>(Player.InputX) },
				{ "InputY", static_cast<int>(Player.InputY) }, { "ConstantX", Player.ConstantX },
				{ "ConstantY", Player.ConstantY }, { "PlayRate", Player.PlayRate },
				{ "Loop", Player.bLoop }, { "X", Player.X }, { "Y", Player.Y } });
		Data["States"].push_back({ { "Id", State.Id }, { "Name", State.Name }, { "X", State.X }, { "Y", State.Y },
			{ "Pose", static_cast<int>(State.Pose) }, { "Asset", State.AssetPath },
			{ "InputX", static_cast<int>(State.InputX) }, { "InputY", static_cast<int>(State.InputY) },
			{ "ConstantX", State.ConstantX }, { "ConstantY", State.ConstantY },
			{ "PlayRate", State.PlayRate }, { "Loop", State.bLoop }, { "PoseConnected", State.bPoseConnected },
			{ "OutputPlayerId", State.OutputPlayerId }, { "Players", std::move(Players) },
			{ "PlayerOffsetX", State.PlayerOffsetX }, { "PlayerOffsetY", State.PlayerOffsetY },
			{ "OutputOffsetX", State.OutputOffsetX }, { "OutputOffsetY", State.OutputOffsetY },
			{ "SpeedOffsetX", State.SpeedOffsetX }, { "SpeedOffsetY", State.SpeedOffsetY },
			{ "DirectionOffsetX", State.DirectionOffsetX }, { "DirectionOffsetY", State.DirectionOffsetY } });
	}
	for (const auto& Transition : Transitions)
		Data["Transitions"].push_back({ { "Id", Transition.Id }, { "From", Transition.From }, { "To", Transition.To },
			{ "Input", static_cast<int>(Transition.Input) }, { "Compare", static_cast<int>(Transition.Compare) },
			{ "Threshold", Transition.Threshold }, { "BlendTime", Transition.BlendTime } });
	std::error_code Ec;
	fs::create_directories(File.parent_path(), Ec);
	if (Ec) { Error = "Cannot create AnimGraph directory."; return false; }
	const fs::path Temp = File.string() + ".editing";
	std::ofstream Out(Temp, std::ios::trunc);
	if (!Out) { Error = "Cannot write AnimGraph."; return false; }
	Out << Data.dump(2);
	Out.close();
	if (!Out) { Error = "Cannot finish writing AnimGraph."; fs::remove(Temp, Ec); return false; }
	if (!MoveFileExW(Temp.c_str(), File.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{ Error = "Cannot replace AnimGraph."; fs::remove(Temp, Ec); return false; }
	Error.clear();
	return true;
}
