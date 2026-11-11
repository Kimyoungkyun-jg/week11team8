#include "EnginePCH.h"
#include "Animation/AnimNotify.h"
#include "Asset/AnimSequence.h"

void UAnimNotify_Log::Notify(const FAnimNotifyContext& Context)
{
	const FString SequencePath = Context.Sequence ? Context.Sequence->GetPath() : FString("None");
	const FString EventName = Context.Event ? Context.Event->Name : FString("None");
	const float TriggerTime = Context.Event ? Context.Event->TriggerTime : 0.0f;
	HTR_LOG(Info, "[AnimNotify] {} '{}' @ {:.3f}s weight {:.2f} ({}) {}", GetClass()->Name, EventName, TriggerTime, Context.Weight, SequencePath, Message);
}
