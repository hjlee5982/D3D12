#pragma once

#include "Component.h"
#include "EngineAPI.h"
#include "Types.h"

#include <vector>

class ENGINE_API Transform : public Component
{
	COMPONENT_TYPE(Transform, Component)

public:
	// 생성자를 Engine.dll 안에 정의해 vtable이 항상 Engine 모듈을 가리키게 한다 (Game.dll 리로드와 무관하게 유지).
	Transform();
	static Transform* CreateInstance();
	~Transform() override;

	const vec3& GetPosition() const;
	void SetPosition(const vec3& position);
	void SetPosition(f32 x, f32 y, f32 z);

	const quat& GetRotation() const;
	void SetRotation(const quat& rotation);

	const vec3& GetScale() const;
	void SetScale(const vec3& scale);
	void SetScale(f32 x, f32 y, f32 z);
	void SetScale(f32 uniformScale);

	void Translate(const vec3& delta);
	void Translate(f32 x, f32 y, f32 z);

	Transform* GetParent() const;
	void SetParent(Transform* parent);

	u32 GetChildCount() const;
	Transform* GetChild(u32 index) const;

	matx GetLocalMatrix() const;
	matx GetWorldMatrix() const;

	vec3 GetWorldPosition() const;

	void OnDestroy() override;

	// 에디터/직렬화용 필드 (position, rotation, scale)
	static void Reflect(TypeBuilder<Transform>& type);

private:
	bool IsDescendantOf(const Transform* ancestor) const;
	void RemoveFromParent();
	void AddChild(Transform* child);
	void RemoveChild(Transform* child);

	vec3 _position{ 0.0f, 0.0f, 0.0f };
	quat _rotation = quat::Identity;
	vec3 _scale{ 1.0f, 1.0f, 1.0f };

	Transform* _parent = nullptr;
	std::vector<Transform*> _children;
};

ENGINE_COMPONENT(Transform);
