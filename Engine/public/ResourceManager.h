#pragma once

#include "EngineAPI.h"
#include "Handle.h"
#include "Types.h"

#include <memory>
#include <string>
#include <vector>

struct ID3D11Device;

// 에디터 등에서 리소스 목록을 보여줄 때 쓰는 항목 (name: 텍스처/셰이더는 에셋 경로, 머티리얼/메시는 이름)
template <typename H>
struct ResourceEntry
{
	H           handle;
	std::string name;
};

// 렌더링 리소스 관리자. 모든 리소스는 Engine이 소유하고 Game 코드는 핸들만 저장한다.
//
//  - 핸들은 세대(generation) 기반: Destroy된 리소스의 핸들은 Get()에서 nullptr이 되므로
//    삭제 후 접근해도 크래시가 나지 않는다 (렌더러는 해당 오브젝트를 건너뛴다).
//  - 같은 경로/이름은 한 번만 만들고 같은 핸들을 돌려준다.
//  - 경로는 Assets 폴더 기준 상대 경로(UTF-8). 예) "Texture/Proto.png"
//
// Mesh/Texture/Shader/Material 정의는 그래픽스 백엔드 쪽 private 헤더에 있다.
// Get()이 돌려주는 포인터는 저장하지 말고 그 자리에서만 쓴다.
class ENGINE_API ResourceManager
{
public:
	explicit ResourceManager(ID3D11Device* device);
	~ResourceManager();

	ResourceManager(const ResourceManager&) = delete;
	ResourceManager& operator=(const ResourceManager&) = delete;

	// --- 생성 / 로드 (실패하면 무효 핸들, 원인은 Engine.log) ---
	TextureHandle LoadTexture(const std::string& assetPath);
	ShaderHandle  LoadShader(const std::string& assetPath);

	// 기본 도형: 중심이 원점인 1x1 사각형 (XY 평면, -Z를 바라봄)
	MeshHandle GetQuadMesh();

	// 이름이 같은 머티리얼이 있으면 그것을 반환한다.
	MaterialHandle CreateMaterial(const std::string& name, ShaderHandle shader, TextureHandle texture);

	// --- 조회 ---
	TextureHandle  FindTexture(const std::string& assetPath) const;
	ShaderHandle   FindShader(const std::string& assetPath) const;
	MaterialHandle FindMaterial(const std::string& name) const;

	// 이름으로 메시를 찾는다. 기본 도형("Quad")은 아직 없으면 만든다.
	MeshHandle FindMesh(const std::string& name);

	Mesh*     Get(MeshHandle handle) const;
	Texture*  Get(TextureHandle handle) const;
	Shader*   Get(ShaderHandle handle) const;
	Material* Get(MaterialHandle handle) const;

	bool IsAlive(MeshHandle handle) const     { return Get(handle) != nullptr; }
	bool IsAlive(TextureHandle handle) const  { return Get(handle) != nullptr; }
	bool IsAlive(ShaderHandle handle) const   { return Get(handle) != nullptr; }
	bool IsAlive(MaterialHandle handle) const { return Get(handle) != nullptr; }

	std::vector<ResourceEntry<MeshHandle>>     ListMeshes() const;
	std::vector<ResourceEntry<TextureHandle>>  ListTextures() const;
	std::vector<ResourceEntry<ShaderHandle>>   ListShaders() const;
	std::vector<ResourceEntry<MaterialHandle>> ListMaterials() const;

	ShaderHandle  GetMaterialShader(MaterialHandle material) const;
	TextureHandle GetMaterialTexture(MaterialHandle material) const;

	// --- 핫 리로드: 같은 핸들에 내용만 교체한다 (실패하면 이전 내용 유지) ---
	bool ReloadTexture(TextureHandle handle);
	bool ReloadShader(ShaderHandle handle);

	// 로드된 텍스처/셰이더 파일이 바뀌었으면 다시 로드한다. Engine이 매 프레임 호출하며
	// 실제 검사는 0.25초 간격. 저장 도중인 파일을 읽지 않도록 수정 시간이 두 번 연속 같을 때 로드한다.
	void PollFileChanges();
	void SetFileWatchEnabled(bool enabled);
	bool IsFileWatchEnabled() const;

	// --- 수정 ---
	bool SetMaterialShader(MaterialHandle material, ShaderHandle shader);
	bool SetMaterialTexture(MaterialHandle material, TextureHandle texture);

	// --- 삭제 (이 리소스를 가리키던 핸들은 모두 무효가 된다) ---
	bool Destroy(MeshHandle handle);
	bool Destroy(TextureHandle handle);
	bool Destroy(ShaderHandle handle);
	bool Destroy(MaterialHandle handle);

	struct Stats
	{
		u32 meshes;
		u32 textures;
		u32 shaders;
		u32 materials;
	};
	Stats GetStats() const;

	// Client.exe 위치에서 위로 올라가며 찾은 Assets 폴더 (UTF-8, 끝에 구분자 포함)
	const std::string& GetAssetRoot() const;
	std::string ResolveAssetPath(const std::string& assetPath) const;

	void Clear();

private:
	struct Impl; // 풀과 이름 테이블 (레이아웃을 공개 헤더에서 숨겨 ABI를 고정)
	std::unique_ptr<Impl> _impl;
};
