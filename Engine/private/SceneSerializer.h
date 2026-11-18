#pragma once

#include <string>

class ComponentRegistry;
class ResourceManager;
class Scene;
class SceneManager;

// 씬 ↔ JSON 파일 (Engine 내부 전용).
//
// 형식 (format 1)
//   {
//     "format": 1, "name": "Main",
//     "materials": [ { "name": "Proto", "shader": "Shader/Default.hlsl", "texture": "Texture/Proto.png" } ],
//     "objects": [
//       { "id": 1, "name": "Parent", "active": true, "parent": null,
//         "components": [ { "type": "Transform", "enabled": true, "fields": { "position": [0, 0, 0], ... } } ] }
//     ]
//   }
//
//  - 컴포넌트는 레지스트리에 등록된 타입/필드만 저장한다 (핫 리로드 스냅샷과 같은 규칙)
//  - 오브젝트 ID는 파일 안에서만 쓰는 값: 불러올 때 새 ID를 받고 ObjectRef 필드를 새 ID로 바꿔 연결한다
//  - 리소스 핸들은 경로/이름으로 저장하고, 불러올 때 다시 로드하거나 찾는다 (핸들 값은 실행마다 다르다)
//  - 불러올 때 필드는 이름으로 맞춘다: 없는 필드는 기본값, 모르는 필드/타입은 경고 후 무시
namespace SceneSerializer
{
	struct Result
	{
		bool        ok         = false;
		std::string error;
		unsigned    objects    = 0;
		unsigned    components = 0;
		unsigned    skipped    = 0; // 등록되지 않은 타입 등으로 저장/복원하지 못한 컴포넌트
	};

	struct LoadOptions
	{
		bool preserveIds    = false; // 파일의 오브젝트 ID를 그대로 사용 (재생 정지 복원: 선택/참조 유지)
		bool applyMaterials = true;  // 머티리얼 정의를 적용 (재생 복원에서는 리소스를 되돌리지 않으므로 false)
	};

	Result Save(const Scene& scene, const ComponentRegistry& registry, const ResourceManager* resources, const std::wstring& filePath);

	// 파일을 읽고 파싱에 성공해야만 활성 씬을 교체한다 (실패하면 기존 씬 유지)
	Result Load(SceneManager& sceneManager, const ComponentRegistry& registry, ResourceManager* resources, const std::wstring& filePath);

	// 메모리 버전 (재생 시작 시 스냅샷, 정지 시 복원)
	Result SaveToString(const Scene& scene, const ComponentRegistry& registry, const ResourceManager* resources, std::string& out);
	Result LoadFromString(SceneManager& sceneManager, const ComponentRegistry& registry, ResourceManager* resources,
		const std::string& text, const LoadOptions& options);
}
