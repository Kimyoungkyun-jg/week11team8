#pragma once
#include "Components/SkeletalMeshComponent.h"
#include "SkeletalMeshDebugComponent.generated.h"
class FLineBatcher;

UCLASS()
class USkeletalMeshDebugComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()
public:
	USkeletalMeshDebugComponent();
	~USkeletalMeshDebugComponent() override;
	void SetSkeletalMesh(USkeletalMesh* Mesh) override;
	bool ShouldDrawDebug() const { return bDrawBones || bDrawBoneAxes; }
	void DrawDebug(FLineBatcher* Lines) const;
	void SetSelectedBone(int32 Bone);
	int32 GetSelectedBone() const { return SelectedBoneIndex; }
	void GetBoneSpheres(TArray<FVector>& Joints, TArray<float>& Radii) const;
	UPROPERTY(EditAnywhere)
	bool bDrawBones = false;
	UPROPERTY(EditAnywhere)
	bool bDrawBoneAxes = false;
	UPROPERTY(EditAnywhere)
	float BoneAxisLength = 0.05f;
	UPROPERTY(EditAnywhere)
	float BoneRadiusScale = 0.005f;
private:
	int32 SelectedBoneIndex = -1;
};
