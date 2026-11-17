#include "pch.h"
#include "Camera.h"
#include "ComponentRegistry.h"
#include "GameObject.h"
#include "Transform.h"

Camera::Camera() = default;

Camera::~Camera() = default;

Camera* Camera::CreateInstance()
{
	return new Camera();
}

matx Camera::GetViewMatrix() const
{
	const Transform* transform = _owner != nullptr ? _owner->transform : nullptr;
	if (transform == nullptr)
	{
		return matx::Identity;
	}

	return transform->GetWorldMatrix().Invert();
}

matx Camera::GetProjectionMatrix(f32 aspectRatio) const
{
	// SimpleMath의 CreatePerspectiveFieldOfView는 오른손 좌표계라서 DirectXMath의 LH 버전을 사용
	return DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(fovYDegrees), aspectRatio, nearZ, farZ);
}

void Camera::Reflect(TypeBuilder<Camera>& type)
{
	type.Field("fovYDegrees", &Camera::fovYDegrees)
		.Field("nearZ", &Camera::nearZ)
		.Field("farZ", &Camera::farZ);
}
