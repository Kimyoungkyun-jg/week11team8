local detectionRadius = 12.0
local attackRadius = 1.2
local attackCooldown = 1.8
local walkSpeed = 6.0

local function ApplyTuning()
    assert(obj:SetZombieCombatTuning(detectionRadius, attackRadius, attackCooldown, walkSpeed))
end

function BeginPlay()
    ApplyTuning()
end

function OnReload()
    ApplyTuning()
end
