#pragma once

#include "GameFramework/Character.h"
#include "TestCharacter.generated.h"

UCLASS()
class ATestCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	ATestCharacter();
	void Jump() override;
	void Attack() override;
	void LockJumpUntilLandingAnimation() { bJumpLocked = true; }
	void OnLandingAnimationFinished() { bJumpLocked = false; }
	void OnAttackAnimationStarted() { bAttackLocked = true; }
	void OnAttackAnimationFinished() { bAttackLocked = false; }
private:
	bool bJumpLocked = false;
	bool bAttackLocked = false;
};
