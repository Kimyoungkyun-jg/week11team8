#include "EnginePCH.h"
#include "FbxImporter.h"
#include <fbxsdk.h>
#include "Render/SkeletalMeshData.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Math/Box.h"
#include "SkeletalMeshBake.h"
#include <filesystem>
#include <cmath>

using namespace fbxsdk;

namespace
{
    template <typename TFbxMatrix>
    FMatrix ConvertFbxMatrix(const TFbxMatrix& FbxMat)
    {
        FMatrix Result;
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                Result.M[row][col] = (float)FbxMat.Get(row, col);
            }
        }
        return Result;
    }

    void ProcessSkeletonRecursive(fbxsdk::FbxNode* Node, int32 ParentIndex, FFbxInfo& Out)
    {
        int32 CurrentBoneIndex = ParentIndex;

        // 노드의 속성이 eSkeleton 또는 eNull(더미/루트 노드)인지 확인
        if (Node->GetNodeAttribute())
        {
            auto AttrType = Node->GetNodeAttribute()->GetAttributeType();
            if (AttrType == fbxsdk::FbxNodeAttribute::eSkeleton || AttrType == fbxsdk::FbxNodeAttribute::eNull)
            {
                FBoneInfo Bone;
                Bone.Name = Node->GetName();
                Bone.ParentIndex = ParentIndex;

                fbxsdk::FbxAMatrix LocalMatrix = Node->EvaluateLocalTransform();
                Bone.LocalBindPose = ConvertFbxMatrix(LocalMatrix);
                Bone.GlobalBindPose = ConvertFbxMatrix(Node->EvaluateGlobalTransform());
                Bone.InverseBindPose = Bone.GlobalBindPose.Inverse();

                Out.Bones.Add(Bone);
                CurrentBoneIndex = Out.Bones.Num() - 1;
            }
        }

        // 자식 노드들도 순회
        for (int i = 0; i < Node->GetChildCount(); ++i)
        {
            ProcessSkeletonRecursive(Node->GetChild(i), CurrentBoneIndex, Out);
        }
    }

    void ProcessSkeleton(fbxsdk::FbxNode* RootNode, FFbxInfo& Out)
    {
        // 루트 밑의 자식들부터 탐색 시작
        for (int i = 0; i < RootNode->GetChildCount(); ++i)
        {
            ProcessSkeletonRecursive(RootNode->GetChild(i), -1, Out);
        }
    }

    void ProcessPolygons(fbxsdk::FbxMesh* Mesh, FFbxInfo& Out, const fbxsdk::FbxAMatrix& MeshMatrix, int32 BaseIndex)
    {
        const fbxsdk::FbxAMatrix NormalMatrix = MeshMatrix.Inverse().Transpose();
        int PolygonCount = Mesh->GetPolygonCount();
        Out.Polygons.Reserve(Out.Polygons.Num() + PolygonCount);

        int PolygonVertexCounter = 0; // 전체 꼭짓점 순차 인덱스

        fbxsdk::FbxGeometryElementMaterial* MaterialElement = Mesh->GetElementMaterialCount() > 0
            ? Mesh->GetElementMaterial(0) : nullptr;

        for (int polyIdx = 0; polyIdx < PolygonCount; ++polyIdx)
        {
            FFbxPolygon Polygon;
            Polygon.MaterialIndex = 0; // 기본 머티리얼 0번

            if (MaterialElement)
            {
                int localMtlIdx = -1;
                // eByPolygon 방식인 경우
                if (MaterialElement->GetMappingMode() == fbxsdk::FbxGeometryElement::eByPolygon)
                {
                    if (MaterialElement->GetReferenceMode() == fbxsdk::FbxGeometryElement::eIndex ||
                        MaterialElement->GetReferenceMode() == fbxsdk::FbxGeometryElement::eIndexToDirect)
                    {
                        localMtlIdx = MaterialElement->GetIndexArray().GetAt(polyIdx);
                    }
                }
                // eAllSame 방식인 경우 (전체 메쉬가 단일 머티리얼 사용)
                else if (MaterialElement->GetMappingMode() == fbxsdk::FbxGeometryElement::eAllSame)
                {
                    localMtlIdx = MaterialElement->GetIndexArray().GetAt(0);
                }

                if (localMtlIdx >= 0)
                {
                    fbxsdk::FbxNode* Node = Mesh->GetNode();
                    if (Node && localMtlIdx < Node->GetMaterialCount())
                    {
                        fbxsdk::FbxSurfaceMaterial* FbxMat = Node->GetMaterial(localMtlIdx);
                        if (FbxMat)
                        {
                            FString MatName = FbxMat->GetName();
                            for (int m = 0; m < Out.MaterialInfos.Num(); ++m)
                            {
                                if (Out.MaterialInfos[m].Name == MatName)
                                {
                                    Polygon.MaterialIndex = m; // 전역 인덱스로 정확히 매핑!
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            // 반사 (음수 스케일) 이 있는 변환이면 Winding 순서가 뒤집히므로 되돌린다.
            const bool bMirrored = MeshMatrix.Determinant() < 0.0f;
            for (int corner = 0; corner < 3; ++corner)
            {
                FFbxCorner& Corner = Polygon.Corners[bMirrored ? 2 - corner : corner];

                // ControlPoint 인덱스
                int cpIndex = Mesh->GetPolygonVertex(polyIdx, corner);
                Corner.ControlPointIndex = BaseIndex + cpIndex;
                Corner.PolygonVertexIndex = PolygonVertexCounter;

                // Normal 추출
                fbxsdk::FbxVector4 FbxNorm;
                if (Mesh->GetPolygonVertexNormal(polyIdx, corner, FbxNorm))
                {
                    fbxsdk::FbxVector4 TransformedNorm = NormalMatrix.MultT(fbxsdk::FbxVector4(FbxNorm[0], FbxNorm[1], FbxNorm[2], 0.0));
                    Corner.Normal = FVector((float)TransformedNorm[0], (float)TransformedNorm[1], (float)TransformedNorm[2]).GetSafeNormal();
                }

                // UV 추출
                if (Mesh->GetElementUVCount() > 0)
                {
                    fbxsdk::FbxGeometryElementUV* ElementUV = Mesh->GetElementUV(0);
                    // 매핑 방식에 따라 UV 읽기
                    fbxsdk::FbxVector2 FbxUV;
                    bool bUnmapped = false;
                    if (Mesh->GetPolygonVertexUV(polyIdx, corner, ElementUV->GetName(), FbxUV, bUnmapped))
                    {
                        // DirectX는 V축이 위에서 아래이므로 (1.0f - V) 뒤집어줍니다.
                        Corner.UVs = FVector2((float)FbxUV[0], 1.0f - (float)FbxUV[1]);
                    }
                }

                PolygonVertexCounter++;
            }

            Out.Polygons.Add(Polygon);
        }
    }

    void ProcessMesh(fbxsdk::FbxNode* Node, FFbxInfo& Out)
    {
        fbxsdk::FbxNodeAttribute* Attribute = Node->GetNodeAttribute();

        // 재귀적으로 씬에서 eMesh 노드를 찾습니다.
        if (Attribute && Attribute->GetAttributeType() == fbxsdk::FbxNodeAttribute::eMesh && Node->GetMesh())
        {
            fbxsdk::FbxMesh* Mesh = Node->GetMesh();

            fbxsdk::FbxAMatrix MeshMatrix;
            MeshMatrix.SetIdentity();
            int SkinCount = Mesh->GetDeformerCount(fbxsdk::FbxDeformer::eSkin);
            if (SkinCount > 0)
            {
                fbxsdk::FbxSkin* Skin = (fbxsdk::FbxSkin*)Mesh->GetDeformer(0, fbxsdk::FbxDeformer::eSkin);
                if (Skin && Skin->GetClusterCount() > 0)
                {
                    Skin->GetCluster(0)->GetTransformMatrix(MeshMatrix);
                }
            }
            else
            {
                MeshMatrix = Node->EvaluateGlobalTransform();
            }

            fbxsdk::FbxPose* BindPose = nullptr;
            int MeshPoseIndex = -1;
            if (fbxsdk::FbxScene* Scene = Node->GetScene())
            {
                for (int PoseIndex = 0; PoseIndex < Scene->GetPoseCount(); ++PoseIndex)
                {
                    fbxsdk::FbxPose* Candidate = Scene->GetPose(PoseIndex);
                    if (Candidate && Candidate->IsBindPose())
                    {
                        const int CandidateMeshIndex = Candidate->Find(Node);
                        if (CandidateMeshIndex >= 0)
                        {
                            BindPose = Candidate;
                            MeshPoseIndex = CandidateMeshIndex;
                            break;
                        }
                    }
                }
            }
            const FMatrix MeshBindGlobal = BindPose
                ? ConvertFbxMatrix(BindPose->GetMatrix(MeshPoseIndex))
                : ConvertFbxMatrix(Node->EvaluateGlobalTransform());
            const FMatrix MeshBindInverse = MeshBindGlobal.Inverse();

            // 원본 정점 위치 읽기
            const int32 BaseIndex = Out.ControlPoints.Num(); // 해당 Mesh의 Control Point 시작 위치
            int ControlPointCount = Mesh->GetControlPointsCount();
            fbxsdk::FbxVector4* ControlPoints = Mesh->GetControlPoints();

            Out.ControlPoints.SetNum(BaseIndex + ControlPointCount);
            Out.ControlPointWeights.SetNum(BaseIndex + ControlPointCount);

            for (int i = 0; i < ControlPointCount; ++i)
            {
                // FBX 좌표를 엔진 FVector로 복사
                fbxsdk::FbxVector4 TransformedPos = MeshMatrix.MultT(ControlPoints[i]);
                Out.ControlPoints[BaseIndex + i] = FVector(
                    static_cast<float>(TransformedPos[0]),
                    static_cast<float>(TransformedPos[1]),
                    static_cast<float>(TransformedPos[2])
                );
            }

            // cluster 또는 BindPose에서 바인드 포즈를 받은 본 표시
            // TArray<bool>은 std::vector<bool> 프록시라 operator[]가 bool&를 못 돌려주므로 uint8 사용
            TArray<uint8> bHasBindData;
            bHasBindData.Init(0, Out.Bones.Num());

            // 스킨 디포머 파싱
            //int SkinCount = Mesh->GetDeformerCount(fbxsdk::FbxDeformer::eSkin);
            for (int i = 0; i < SkinCount; ++i)
            {
                fbxsdk::FbxSkin* Skin = (fbxsdk::FbxSkin*)Mesh->GetDeformer(i, fbxsdk::FbxDeformer::eSkin);
                int ClusterCount = Skin->GetClusterCount();

                for (int clusterIdx = 0; clusterIdx < ClusterCount; ++clusterIdx)
                {
                    fbxsdk::FbxCluster* Cluster = Skin->GetCluster(clusterIdx);
                    fbxsdk::FbxNode* LinkNode = Cluster->GetLink(); // 이 클러스터에 연결된 본 노드
                    if (!LinkNode) continue;

                    // Bones에서 이 본의 인덱스 찾기
                    FString BoneName = LinkNode->GetName();
                    int32 BoneIndex = -1;
                    for (int b = 0; b < Out.Bones.Num(); ++b)
                    {
                        if (Out.Bones[b].Name == BoneName)
                        {
                            BoneIndex = b;
                            break;
                        }
                    }
                    if (BoneIndex == -1) continue;

                    // 바인드 포즈 행렬 및 역행렬 추출
                    fbxsdk::FbxAMatrix TransformLinkMatrix;
                    Cluster->GetTransformLinkMatrix(TransformLinkMatrix); // 본의 초기 월드 행렬
                    FMatrix GlobalBindPose = ConvertFbxMatrix(TransformLinkMatrix);
                    Out.Bones[BoneIndex].GlobalBindPose = GlobalBindPose;
                    Out.Bones[BoneIndex].InverseBindPose = GlobalBindPose.Inverse();
                    bHasBindData[BoneIndex] = 1;

                    // 이 본이 영향을 주는 정점 인덱스와 가중치 읽기
                    int IndexCount = Cluster->GetControlPointIndicesCount();
                    int* Indices = Cluster->GetControlPointIndices();
                    double* Weights = Cluster->GetControlPointWeights();

                    for (int k = 0; k < IndexCount; ++k)
                    {
                        int cpIndex = BaseIndex + Indices[k];
                        float weight = static_cast<float>(Weights[k]);
                        Out.ControlPointWeights[cpIndex].AddBoneWeight(BoneIndex, weight);
                    }
                }
            }

            // 모든 정점의 가중치 합을 1.0으로 정규화
            for (int i = 0; i < ControlPointCount; ++i)
            {
                Out.ControlPointWeights[BaseIndex + i].NormalizeWeights();
            }

            // 정점은 mesh 로컬 공간이므로 bone의 bind pose도 같은 공간으로 맞춘다.
            if (BindPose)
            {
                for (int PoseIndex = 0; PoseIndex < BindPose->GetCount(); ++PoseIndex)
                {
                    fbxsdk::FbxNode* PoseNode = BindPose->GetNode(PoseIndex);
                    if (!PoseNode || PoseNode == Node) continue;
                    for (int32 BoneIndex = 0; BoneIndex < Out.Bones.Num(); ++BoneIndex)
                    {
                        if (Out.Bones[BoneIndex].Name == PoseNode->GetName())
                        {
                            Out.Bones[BoneIndex].GlobalBindPose = ConvertFbxMatrix(BindPose->GetMatrix(PoseIndex));
                            bHasBindData[BoneIndex] = 1;
                            break;
                        }
                    }
                }
            }

            // cluster/BindPose 어디에도 없는 본(_end 등)은 부모 바인드 포즈 기준으로 재계산
            // 좌표계 변환 시 노드에는 회전만, BindPose/cluster에는 반사까지 적용되어 EvaluateGlobalTransform 값과 어긋남
            // Bones는 DFS 전위 순서라 부모가 항상 먼저 계산됨
            for (int32 b = 0; b < Out.Bones.Num(); ++b)
            {
                const int32 p = Out.Bones[b].ParentIndex;
                if (!bHasBindData[b] && p != -1)
                {
                    Out.Bones[b].GlobalBindPose = Out.Bones[b].LocalBindPose * Out.Bones[p].GlobalBindPose;
                }
            }

            for (FBoneInfo& Bone : Out.Bones)
            {
                Bone.InverseBindPose = Bone.GlobalBindPose.Inverse();
            }

            // 클러스터가 없는 루트 eNull 노드가 있을 경우 자식의 바인드 포즈로부터 역산
            for (int32 b = 0; b < Out.Bones.Num(); ++b)
            {
                if (Out.Bones[b].ParentIndex != -1)
                {
                    int32 p = Out.Bones[b].ParentIndex;
                    if (Out.Bones[p].GlobalBindPose == FMatrix::Identity && Out.Bones[b].GlobalBindPose != FMatrix::Identity)
                    {
                        Out.Bones[p].GlobalBindPose = Out.Bones[b].GlobalBindPose * Out.Bones[b].LocalBindPose.Inverse();
                        Out.Bones[p].InverseBindPose = Out.Bones[p].GlobalBindPose.Inverse();
                    }
                }
            }

            // 부모-자식 계층에 맞게 LocalBindPose를 GlobalBindPose 기준으로 일치화
            for (int32 b = 0; b < Out.Bones.Num(); ++b)
            {
                if (Out.Bones[b].ParentIndex == -1)
                {
                    Out.Bones[b].LocalBindPose = Out.Bones[b].GlobalBindPose;
                }
                else
                {
                    int32 p = Out.Bones[b].ParentIndex;
                    Out.Bones[b].LocalBindPose = Out.Bones[b].GlobalBindPose * Out.Bones[p].GlobalBindPose.Inverse();
                }
            }

            // 폴리곤 데이터 읽기
            ProcessPolygons(Mesh, Out, MeshMatrix, BaseIndex);
        }

        for (int i = 0; i < Node->GetChildCount(); ++i)
        {
            ProcessMesh(Node->GetChild(i), Out);
        }
    }

    FString GetTexturePathFromProperty(fbxsdk::FbxProperty Property, const FString& FbxDirectory)
    {
        if (!Property.IsValid()) return "";

        int TextureCount = Property.GetSrcObjectCount<fbxsdk::FbxFileTexture>();
        if (TextureCount > 0)
        {
            fbxsdk::FbxFileTexture* FileTexture = Property.GetSrcObject<fbxsdk::FbxFileTexture>(0);
            if (FileTexture)
            {
                // 상대 경로로 합쳐서 실제 파일이 있는지 확인
                FString RelPath = FileTexture->GetRelativeFileName();
                if (!RelPath.empty())
                {
                    std::filesystem::path Combined = std::filesystem::path(FbxDirectory.c_str()) / RelPath.c_str();
                    if (std::filesystem::exists(Combined))
                        return Combined.lexically_normal().generic_string().c_str();
                }

                // 같은 폴더에 파일명으로 존재하는지 확인 (예: Assets/Models/.../shaded.png)
                std::filesystem::path FullPath = FileTexture->GetFileName();
                std::filesystem::path FileName = FullPath.filename();
                std::filesystem::path SameDir = std::filesystem::path(FbxDirectory.c_str()) / FileName;
                if (std::filesystem::exists(SameDir))
                    return SameDir.lexically_normal().generic_string().c_str();

                // FBX 자동 언팩 폴더(.fbm) 내에 존재하는지 확인
                std::filesystem::path FbxPath(FbxDirectory.c_str());
                for (const auto& Entry : std::filesystem::directory_iterator(FbxDirectory.c_str()))
                {
                    if (Entry.is_directory())
                    {
                        std::filesystem::path InSubDir = Entry.path() / FileName;
                        if (std::filesystem::exists(InSubDir))
                            return InSubDir.lexically_normal().generic_string().c_str();
                    }
                }
            }
        }
        return "";
    }

    void ProcessMaterials(fbxsdk::FbxNode* Node, FFbxInfo& Out)
    {
        // FBX 파일이 위치한 부모 폴더 경로 (텍스처 상대 경로 조합용)
        std::filesystem::path FbxFilePath(Out.Path.c_str());
        FString FbxDir = FbxFilePath.parent_path().string().c_str();

        int MaterialCount = Node->GetMaterialCount();
        for (int i = 0; i < MaterialCount; ++i)
        {
            fbxsdk::FbxSurfaceMaterial* FbxMat = Node->GetMaterial(i);
            if (!FbxMat) continue;

            FString MatName = FbxMat->GetName();

            bool bAlreadyExists = false;
            for (const auto& Exist : Out.MaterialInfos)
            {
                if (Exist.Name == MatName) { bAlreadyExists = true; break; }
            }
            if (bAlreadyExists) continue;

            FFbxMaterialInfo MatInfo;
            MatInfo.Name = MatName;

            // Diffuse / BaseColor 색상 읽기
            if (FbxMat->GetClassId().Is(fbxsdk::FbxSurfaceLambert::ClassId))
            {
                fbxsdk::FbxDouble3 Diffuse = ((fbxsdk::FbxSurfaceLambert*)FbxMat)->Diffuse.Get();
                MatInfo.BaseColor = FVector4((float)Diffuse[0], (float)Diffuse[1], (float)Diffuse[2], 1.0f);
                auto* Lambert = static_cast<fbxsdk::FbxSurfaceLambert*>(FbxMat);
                const float DiffuseFactor = static_cast<float>(Lambert->DiffuseFactor.Get());
                MatInfo.BaseColor.X *= DiffuseFactor;
                MatInfo.BaseColor.Y *= DiffuseFactor;
                MatInfo.BaseColor.Z *= DiffuseFactor;
                MatInfo.BaseColor.W = std::clamp(1.0f - static_cast<float>(Lambert->TransparencyFactor.Get()), 0.0f, 1.0f);
                const auto Emissive = Lambert->Emissive.Get();
                MatInfo.EmissiveColor = FVector(static_cast<float>(Emissive[0]), static_cast<float>(Emissive[1]), static_cast<float>(Emissive[2]));
                MatInfo.EmissiveFactor = static_cast<float>(Lambert->EmissiveFactor.Get());
                MatInfo.ShadingModel = ELightingModel::Lambert;
            }
            if (FbxMat->GetClassId().Is(fbxsdk::FbxSurfacePhong::ClassId))
            {
                auto* Phong = static_cast<fbxsdk::FbxSurfacePhong*>(FbxMat);
                const auto Specular = Phong->Specular.Get();
                MatInfo.SpecularColor = FVector(static_cast<float>(Specular[0]), static_cast<float>(Specular[1]), static_cast<float>(Specular[2]));
                MatInfo.SpecularFactor = static_cast<float>(Phong->SpecularFactor.Get());
                MatInfo.Shininess = std::max(1.0f, static_cast<float>(Phong->Shininess.Get()));
                MatInfo.ShadingModel = ELightingModel::Phong;
            }

            // Diffuse 텍스처 경로 읽기
            fbxsdk::FbxProperty DiffuseProp = FbxMat->FindProperty(fbxsdk::FbxSurfaceMaterial::sDiffuse);
            MatInfo.DiffuseTexturePath = GetTexturePathFromProperty(DiffuseProp, FbxDir);

            // 만약 PBR 머티리얼이라 sDiffuse가 비어있다면 "BaseColor" 속성도 검사
            if (MatInfo.DiffuseTexturePath.empty())
            {
                fbxsdk::FbxProperty BaseColorProp = FbxMat->FindProperty("BaseColor");
                MatInfo.DiffuseTexturePath = GetTexturePathFromProperty(BaseColorProp, FbxDir);
            }

            // Normal 텍스처 경로 읽기
            fbxsdk::FbxProperty NormalProp = FbxMat->FindProperty(fbxsdk::FbxSurfaceMaterial::sNormalMap);
            MatInfo.NormalTexturePath = GetTexturePathFromProperty(NormalProp, FbxDir);

            if (MatInfo.NormalTexturePath.empty())
            {
                fbxsdk::FbxProperty BumpProp = FbxMat->FindProperty(fbxsdk::FbxSurfaceMaterial::sBump);
                MatInfo.NormalTexturePath = GetTexturePathFromProperty(BumpProp, FbxDir);
            }

            Out.MaterialInfos.Add(MatInfo);
        }

        // 자식 노드 순회
        for (int i = 0; i < Node->GetChildCount(); ++i)
        {
            ProcessMaterials(Node->GetChild(i), Out);
        }
    }

    void CollectAnimationBones(fbxsdk::FbxNode* Node, int32 ParentIndex,
        TArray<fbxsdk::FbxNode*>& Nodes, TArray<FString>& Names, TArray<int32>& Parents)
    {
        int32 CurrentParent = ParentIndex;
        if (fbxsdk::FbxNodeAttribute* Attribute = Node->GetNodeAttribute())
        {
            const auto Type = Attribute->GetAttributeType();
            if (Type == fbxsdk::FbxNodeAttribute::eSkeleton || Type == fbxsdk::FbxNodeAttribute::eNull)
            {
                CurrentParent = Nodes.Num();
                Nodes.Add(Node);
                Names.Add(Node->GetName());
                Parents.Add(ParentIndex);
            }
        }
        for (int Child = 0; Child < Node->GetChildCount(); ++Child)
            CollectAnimationBones(Node->GetChild(Child), CurrentParent, Nodes, Names, Parents);
    }
    // 정점을 공유해도 되는지 판단하는 키
    // 컨트롤 포인트 (위치, 뼈 가중치) + 노멀 + UV
    // float는 비트 그대로 비교 및 해싱. 같은 fbx 데이터에서 온 값이므로 공유할 꼭짓점은 비트까지 같아야함
    struct FCornerKey
    {
        uint32 Bits[6] = {}; // ControlPointIndex, Normal.XYZ, UV.XY

        bool operator==(const FCornerKey& Other) const
        {
            return std::memcmp(Bits, Other.Bits, sizeof(Bits)) == 0;
        }
    };

    struct FCornerKeyHash
    {
        size_t operator()(const FCornerKey& Key) const
        {
            size_t Hash = 0;
            for (uint32 Value : Key.Bits)
                Hash ^= Value + 0x9e3779b9 + (Hash << 6) + (Hash >> 2); // boost::hash_combine과 같은 로직
            return Hash;
        }
    };

    FCornerKey MakeCornerKey(const FFbxCorner& Corner)
    {
        FCornerKey Key;
        Key.Bits[0] = static_cast<uint32>(Corner.ControlPointIndex);
        std::memcpy(&Key.Bits[1], &Corner.Normal.X, sizeof(float));
        std::memcpy(&Key.Bits[2], &Corner.Normal.Y, sizeof(float));
        std::memcpy(&Key.Bits[3], &Corner.Normal.Z, sizeof(float));
        std::memcpy(&Key.Bits[4], &Corner.UVs.X, sizeof(float));
        std::memcpy(&Key.Bits[5], &Corner.UVs.Y, sizeof(float));
        return Key;
    }
}

bool FFbxImporter::ParseFbx(const FString& Path, FFbxInfo& Out)
{
    // FBX SDK 초기화 및 파일 로드, 삼각형화
    fbxsdk::FbxManager* Manager = fbxsdk::FbxManager::Create();
    if (!Manager) return false;

    fbxsdk::FbxIOSettings* IOSettings = fbxsdk::FbxIOSettings::Create(Manager, IOSROOT);
    Manager->SetIOSettings(IOSettings);

    fbxsdk::FbxImporter* Importer = fbxsdk::FbxImporter::Create(Manager, "");
    // 한글 경로 등 대비 c_str() 전달
    if (!Importer->Initialize(Path.c_str(), -1, Manager->GetIOSettings()))
    {
        Manager->Destroy();
        return false;
    }

    fbxsdk::FbxScene* Scene = fbxsdk::FbxScene::Create(Manager, "Scene");
    Importer->Import(Scene);
    Importer->Destroy(); // 씬에 올렸으면 임포터 객체는 바로 닫아도 됩니다.

    // 크기 단위 변환, 원본 단위 / 목표 단위로 배율을 구하고 모든 정점의 좌표에 곱해줌, Bone의 이동거리와 애니메이션 키프레임의 위치 값에도 곱해줌
    fbxsdk::FbxSystemUnit SceneUnit = Scene->GetGlobalSettings().GetSystemUnit();
    if (SceneUnit != fbxsdk::FbxSystemUnit::m)
    {
        fbxsdk::FbxSystemUnit::m.ConvertScene(Scene);
    }

	// 좌표계 변환 (Z-Up, Left-Handed)
    fbxsdk::FbxAxisSystem EngineAxisSystem(
        fbxsdk::FbxAxisSystem::eZAxis,
        fbxsdk::FbxAxisSystem::eParityEven,
        fbxsdk::FbxAxisSystem::eLeftHanded
    );

	// 씬의 좌표계를 엔진 좌표계로 변환
    fbxsdk::FbxAxisSystem SceneAxisSystem = Scene->GetGlobalSettings().GetAxisSystem();
	// FBX 좌표계와 엔진 좌표계가 다르면 씬 전체를 변환
    if (SceneAxisSystem != EngineAxisSystem)
    {
        // 기저 변환으로 회전시키고 삼각형 감김 순서를 뒤집어서 겉면이 제대로 보이도록 한다.
        EngineAxisSystem.DeepConvertScene(Scene);
    }

    // 삼각화
    fbxsdk::FbxGeometryConverter Converter(Manager);
    Converter.Triangulate(Scene, true);

    fbxsdk::FbxNode* RootNode = Scene->GetRootNode();
    if (!RootNode)
    {
        Manager->Destroy();
        return false;
    }

    Out.Path = Path;

    // 스켈레톤 정보 추출
    ProcessSkeleton(RootNode, Out);

    // 머티리얼 및 텍스처 경로 추출
    ProcessMaterials(RootNode, Out);

    // 메시, 정점, 스키닝 가중치 추출
    ProcessMesh(RootNode, Out);

    Manager->Destroy();
    return true;
}

bool FFbxImporter::LoadAnimationClips(const FString& Path, TArray<FAnimationClip>& OutClips)
{
    OutClips.Reset();
    const FString BinPath = Path + ".anim.bin";
    std::error_code FileError;
    const auto SourceTime = std::filesystem::last_write_time(Path, FileError);
    if (FileError) return false;
    FileError.clear();
    const auto CacheTime = std::filesystem::last_write_time(BinPath, FileError);
    if (!FileError && CacheTime >= SourceTime && FSkeletalMeshBake::ReadAnimations(BinPath, OutClips))
        return true;

    fbxsdk::FbxManager* Manager = fbxsdk::FbxManager::Create();
    if (!Manager) return false;
    fbxsdk::FbxIOSettings* IOSettings = fbxsdk::FbxIOSettings::Create(Manager, IOSROOT);
    Manager->SetIOSettings(IOSettings);
    fbxsdk::FbxImporter* Importer = fbxsdk::FbxImporter::Create(Manager, "");
    if (!Importer->Initialize(Path.c_str(), -1, IOSettings))
    {
        Manager->Destroy();
        return false;
    }
    fbxsdk::FbxScene* Scene = fbxsdk::FbxScene::Create(Manager, "AnimationScene");
    const bool bImported = Importer->Import(Scene);
    Importer->Destroy();
    if (!bImported)
    {
        Manager->Destroy();
        return false;
    }

    if (Scene->GetGlobalSettings().GetSystemUnit() != fbxsdk::FbxSystemUnit::m)
        fbxsdk::FbxSystemUnit::m.ConvertScene(Scene);
    const fbxsdk::FbxAxisSystem EngineAxisSystem(
        fbxsdk::FbxAxisSystem::eZAxis,
        fbxsdk::FbxAxisSystem::eParityEven, 
        fbxsdk::FbxAxisSystem::eLeftHanded);
    if (Scene->GetGlobalSettings().GetAxisSystem() != EngineAxisSystem)
        EngineAxisSystem.DeepConvertScene(Scene);

    TArray<fbxsdk::FbxNode*> BoneNodes;
    TArray<FString> BoneNames;
    TArray<int32> ParentIndices;
    if (fbxsdk::FbxNode* Root = Scene->GetRootNode())
        for (int Child = 0; Child < Root->GetChildCount(); ++Child)
            CollectAnimationBones(Root->GetChild(Child), -1, BoneNodes, BoneNames, ParentIndices);
    if (BoneNodes.IsEmpty() || BoneNodes.Num() > 4096)
    {
        Manager->Destroy();
        return false;
    }
    FString SkeletonName = BoneNames[0];
    for (fbxsdk::FbxNode* BoneNode : BoneNodes)
        if (BoneNode->GetNodeAttribute() &&
            BoneNode->GetNodeAttribute()->GetAttributeType() == fbxsdk::FbxNodeAttribute::eSkeleton)
        {
            SkeletonName = BoneNode->GetName();
            break;
        }

    TArray<FAnimationClip> Clips;
    const int StackCount = Scene->GetSrcObjectCount<fbxsdk::FbxAnimStack>();
    if (StackCount > 256)
    {
        Manager->Destroy();
        return false;
    }
    for (int StackIndex = 0; StackIndex < StackCount; ++StackIndex)
    {
        fbxsdk::FbxAnimStack* Stack = Scene->GetSrcObject<fbxsdk::FbxAnimStack>(StackIndex);
        if (!Stack) continue;
        fbxsdk::FbxTimeSpan Span = Stack->GetLocalTimeSpan();

        if (Span.GetDuration().GetSecondDouble() <= 0.0)
            Scene->GetGlobalSettings().GetTimelineDefaultTimeSpan(Span);

        const double Start = Span.GetStart().GetSecondDouble();
        const double Duration = Span.GetStop().GetSecondDouble() - Start;
        if (!std::isfinite(Start) || !std::isfinite(Duration) || Duration <= 0.0) continue;
        const double FrameCountDouble = std::ceil(Duration * 30.0) + 1.0;
        if (FrameCountDouble > 100000.0 || FrameCountDouble * BoneNodes.Num() > 50000000.0)
        {
            Manager->Destroy();
            return false;
        }

        Scene->SetCurrentAnimationStack(Stack); // 지금 부터 애니메이션 실행 위치 계산
        FAnimationClip Clip;
        Clip.Name = Stack->GetName();
        Clip.SkeletonName = SkeletonName;
        Clip.DurationSeconds = static_cast<float>(Duration);
        Clip.BoneNames = BoneNames;
        Clip.ParentIndices = ParentIndices;
        const int32 FrameCount = static_cast<int32>(FrameCountDouble);
        Clip.Frames.Reserve(FrameCount);
        for (int32 FrameIndex = 0; FrameIndex < FrameCount; ++FrameIndex)
        {
            fbxsdk::FbxTime Time;
            Time.SetSecondDouble(Start + std::min(FrameIndex / 30.0, Duration));
            FAnimationFrame Frame;
            Frame.Bones.SetNum(BoneNodes.Num());
            for (int32 BoneIndex = 0; BoneIndex < BoneNodes.Num(); ++BoneIndex)
                Frame.Bones[BoneIndex] = ConvertFbxMatrix(BoneNodes[BoneIndex]->EvaluateGlobalTransform(Time)); // 위에 선택했던 Stack 에서 time 일 때 각 본의 전역 행렬 반환
            Clip.Frames.Add(std::move(Frame));
        }
        Clips.Add(std::move(Clip));
    }
    Manager->Destroy();
    if (!FSkeletalMeshBake::WriteAnimations(BinPath, Clips)) return false;
    OutClips = std::move(Clips);
    return true;
}

TUniquePtr<FSkeletalMeshData> FFbxImporter::LoadSkeletalMeshData(const FString& Path)
{
    const FString BinPath = Path + ".bin";

    // 바이너리 반환
    if (TUniquePtr<FSkeletalMeshData> Baked = FSkeletalMeshBake::ReadBaked(BinPath))
    {
        return Baked;
    }

    // 캐시가 없으면 FBX 파싱 및 Cook 수행
    FFbxInfo RawInfo;
    if (!ParseFbx(Path, RawInfo))
    {
        return nullptr;
    }

    TUniquePtr<FSkeletalMeshData> MeshData = MakeUnique<FSkeletalMeshData>();
    if (!Cook(RawInfo, *MeshData))
    {
        return nullptr;
    }

    // 가공한 쿡 데이터를 다음 실행을 위해 .bin 파일로 베이크
    FSkeletalMeshBake::WriteBaked(BinPath, *MeshData);

    return MeshData;
}

bool FFbxImporter::Cook(const FFbxInfo& Raw, FSkeletalMeshData& Out)
{
    Out.Bones = Raw.Bones;

    // 같은 꼭짓점은 정점 하나를 공유
    TMap<FCornerKey, uint32, FCornerKeyHash> CornerToVertex;
    CornerToVertex.Reserve(Raw.Polygons.Num() * 3);

    int32 MaterialCount = Raw.MaterialInfos.Num();
    if (MaterialCount == 0)
    {
        FSkeletalMaterialSlot DefaultSlot;
        DefaultSlot.Name = "DefaultMaterial";
        Out.MaterialSlots.Add(DefaultSlot);
        MaterialCount = 1;
    }
    else
    {
        for (const FFbxMaterialInfo& MatInfo : Raw.MaterialInfos)
        {
            FSkeletalMaterialSlot Slot;
            Slot.Name = MatInfo.Name;
            Slot.BaseColor = MatInfo.BaseColor;
            static_cast<FImportedMaterialProperties&>(Slot) = MatInfo;
            Slot.DiffuseTexturePath = MatInfo.DiffuseTexturePath;
            Slot.NormalTexturePath = MatInfo.NormalTexturePath;
            Out.MaterialSlots.Add(Slot);
        }
    }

    // AABB 계산용 초기값
    FVector BoundsMin(FLT_MAX, FLT_MAX, FLT_MAX);
    FVector BoundsMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (int32 mtlIdx = 0; mtlIdx < MaterialCount; ++mtlIdx)
    {
        FSkeletalMeshSection Section;
        Section.StartIndex = static_cast<uint32>(Out.Indices.Num());
        Section.MaterialSlotIndex = mtlIdx;

        // 이 머티리얼 슬롯을 사용하는 삼각형만 모읍니다.
        for (const FFbxPolygon& Poly : Raw.Polygons)
        {
            // 폴리곤의 MaterialIndex가 현재 슬롯과 일치하는지 확인
            int32 PolyMtlIdx = (Poly.MaterialIndex >= 0 && Poly.MaterialIndex < MaterialCount) ? Poly.MaterialIndex : 0;
            if (PolyMtlIdx != mtlIdx) continue;

            const int CornerOrder[3] = { 0, 1, 2 };
            // 삼각형 3개 꼭짓점을 정점으로 변환 (CW 앞면 정렬)
            for (int i = 0; i < 3; ++i)
            {
                int corner = CornerOrder[i];
                const FFbxCorner& Corner = Poly.Corners[corner];
                const FCornerKey Key = MakeCornerKey(Corner);

                if (const uint32* Existing = CornerToVertex.Find(Key))
                {
                    Out.Indices.Add(*Existing);
                    continue;
                }

                int32 cpIdx = Corner.ControlPointIndex;

                FSkeletalVertex SkinnedVertex;

                // 위치(Position)
                if (cpIdx >= 0 && cpIdx < Raw.ControlPoints.Num())
                {
                    SkinnedVertex.Position = Raw.ControlPoints[cpIdx];

                    // AABB 갱신
                    BoundsMin.X = std::min(BoundsMin.X, SkinnedVertex.Position.X);
                    BoundsMin.Y = std::min(BoundsMin.Y, SkinnedVertex.Position.Y);
                    BoundsMin.Z = std::min(BoundsMin.Z, SkinnedVertex.Position.Z);

                    BoundsMax.X = std::max(BoundsMax.X, SkinnedVertex.Position.X);
                    BoundsMax.Y = std::max(BoundsMax.Y, SkinnedVertex.Position.Y);
                    BoundsMax.Z = std::max(BoundsMax.Z, SkinnedVertex.Position.Z);

                    // 본 인덱스 & 가중치 매핑
                    const FFbxBoneWeight& BoneWeight = Raw.ControlPointWeights[cpIdx];
                    for (int k = 0; k < 4; ++k)
                    {
                        SkinnedVertex.BoneIndices[k] = BoneWeight.BoneIndices[k];
                        SkinnedVertex.BoneWeights[k] = BoneWeight.BoneWeights[k];
                    }
                }

                // Normal, UV, Color
                SkinnedVertex.Normal = Corner.Normal;
                SkinnedVertex.UV = Corner.UVs;
                SkinnedVertex.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);

                // 인덱스 및 정점 추가
                uint32 NewVertexIndex = static_cast<uint32>(Out.SkinnedVertices.Num());
                Out.SkinnedVertices.Add(SkinnedVertex);
                Out.Indices.Add(NewVertexIndex);
                
                CornerToVertex.Add(Key, NewVertexIndex);
            }
        }

        Section.IndexCount = static_cast<uint32>(Out.Indices.Num()) - Section.StartIndex;
        if (Section.IndexCount > 0)
        {
            Out.Sections.Add(Section);
        }
    }
    Out.AABB.Min = BoundsMin;
    Out.AABB.Max = BoundsMax;
    Out.BuildTangentSpace();
    return true;
}
