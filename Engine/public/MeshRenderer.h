#pragma once

#include "Component.h"
#include "EngineAPI.h"
#include "Handle.h"

// 메시 + 머티리얼로 오브젝트를 그린다.
// Engine에 정의된 컴포넌트이므로 Game.dll 리로드 때 제거되지 않는다
// (생성자를 Engine.dll 안에 두어 vtable이 Engine 모듈에 있도록 함).
//
// 리소스는 핸들로 가진다: 메시/머티리얼이 삭제되면 렌더러가 이 오브젝트를 건너뛸 뿐 크래시하지 않는다.
class ENGINE_API MeshRenderer : public Component
{
	COMPONENT_TYPE(MeshRenderer, Component)

public:
	MeshRenderer();
	MeshRenderer(MeshHandle mesh, MaterialHandle material);
	~MeshRenderer() override;

	// AddComponent<MeshRenderer>(...)가 사용 (Engine.dll 안에서 생성)
	static MeshRenderer* CreateInstance();
	static MeshRenderer* CreateInstance(MeshHandle mesh, MaterialHandle material);

	MeshHandle     GetMesh() const;
	MaterialHandle GetMaterial() const;

	void SetMesh(MeshHandle mesh);
	void SetMaterial(MaterialHandle material);

	static void Reflect(TypeBuilder<MeshRenderer>& type);

private:
	MeshHandle     _mesh;
	MaterialHandle _material;
};

ENGINE_COMPONENT(MeshRenderer);
