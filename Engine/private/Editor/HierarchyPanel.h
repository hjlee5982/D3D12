#pragma once

#include "EditorContext.h"

#include <string>
#include <vector>

class GameObject;
class Scene;

// Hierarchy 창: 오브젝트 트리, 생성/삭제, 드래그로 부모 변경(월드 위치 유지)
class HierarchyPanel
{
public:
	explicit HierarchyPanel(EditorContext& context) : _ctx(context) {}

	void Draw(Scene* scene);

private:
	EditorContext& _ctx;

	void DrawNode(GameObject* object);
};
