#pragma once

#include "EditorContext.h"

#include <string>
#include <vector>

class Component;
class GameObject;
struct FieldInfo;

// Inspector 창: 선택한 오브젝트의 상세 정보, 리플렉션 필드 편집, 컴포넌트 추가/제거/활성화
class InspectorPanel
{
public:
	explicit InspectorPanel(EditorContext& context) : _ctx(context) {}

	void Draw();

private:
	EditorContext& _ctx;

	void DrawObjectDetails(GameObject* object);
	void DrawComponent(GameObject* object, Component* component, Component*& removeRequest);
	void DrawComponentDetails(Component* component); // MeshRenderer 리소스 정보, 활성 카메라 여부 등
	void DrawField(Component* component, const FieldInfo& field);
};
