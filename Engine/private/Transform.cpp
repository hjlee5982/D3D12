#include "pch.h"
#include "Transform.h"
#include "GameObject.h"

Transform::~Transform()
{
	OnDestroy();
}

const vec3& Transform::GetPosition() const
{
	return _position;
}

void Transform::SetPosition(const vec3& position)
{
	_position = position;
}

void Transform::SetPosition(f32 x, f32 y, f32 z)
{
	_position = vec3(x, y, z);
}

const quat& Transform::GetRotation() const
{
	return _rotation;
}

void Transform::SetRotation(const quat& rotation)
{
	_rotation = rotation;
}

const vec3& Transform::GetScale() const
{
	return _scale;
}

void Transform::SetScale(const vec3& scale)
{
	_scale = scale;
}

void Transform::SetScale(f32 x, f32 y, f32 z)
{
	_scale = vec3(x, y, z);
}

void Transform::SetScale(f32 uniformScale)
{
	_scale = vec3(uniformScale, uniformScale, uniformScale);
}

void Transform::Translate(const vec3& delta)
{
	_position += delta;
}

void Transform::Translate(f32 x, f32 y, f32 z)
{
	_position += vec3(x, y, z);
}

Transform* Transform::GetParent() const
{
	return _parent;
}

void Transform::SetParent(Transform* parent)
{
	if (parent == _parent || parent == this)
	{
		return;
	}

	if (parent != nullptr && parent->IsDescendantOf(this))
	{
		return;
	}

	RemoveFromParent();

	_parent = parent;
	if (_parent != nullptr)
	{
		_parent->AddChild(this);
	}
}

u32 Transform::GetChildCount() const
{
	return static_cast<u32>(_children.size());
}

Transform* Transform::GetChild(u32 index) const
{
	if (index >= _children.size())
	{
		return nullptr;
	}

	return _children[index];
}

matx Transform::GetLocalMatrix() const
{
	return matx::CreateScale(_scale)
		* matx::CreateFromQuaternion(_rotation)
		* matx::CreateTranslation(_position);
}

matx Transform::GetWorldMatrix() const
{
	if (_parent == nullptr)
	{
		return GetLocalMatrix();
	}

	return GetLocalMatrix() * _parent->GetWorldMatrix();
}

vec3 Transform::GetWorldPosition() const
{
	const matx world = GetWorldMatrix();
	return vec3(world._41, world._42, world._43);
}

void Transform::OnDestroy()
{
	while (!_children.empty())
	{
		Transform* child = _children.back();
		child->_parent = nullptr;
		_children.pop_back();
	}

	RemoveFromParent();

	if (_owner != nullptr && _owner->transform == this)
	{
		_owner->transform = nullptr;
	}
}

bool Transform::IsDescendantOf(const Transform* ancestor) const
{
	const Transform* current = _parent;
	while (current != nullptr)
	{
		if (current == ancestor)
		{
			return true;
		}
		current = current->_parent;
	}

	return false;
}

void Transform::RemoveFromParent()
{
	if (_parent == nullptr)
	{
		return;
	}

	_parent->RemoveChild(this);
	_parent = nullptr;
}

void Transform::AddChild(Transform* child)
{
	if (child == nullptr)
	{
		return;
	}

	_children.push_back(child);
}

void Transform::RemoveChild(Transform* child)
{
	const auto it = std::find(_children.begin(), _children.end(), child);
	if (it != _children.end())
	{
		_children.erase(it);
	}
}
