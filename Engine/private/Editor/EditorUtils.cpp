#include "pch.h"
#include "EditorUtils.h"

#include "Transform.h"

#include <cfloat>
#include <cmath>
#include <filesystem>

namespace EditorUtil
{
	std::string ToUtf8(const std::wstring& wide)
	{
		if (wide.empty())
		{
			return {};
		}
		const int size = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
		std::string result(static_cast<size_t>(size), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr);
		return result;
	}

	std::wstring ToWide(const std::string& utf8)
	{
		if (utf8.empty())
		{
			return {};
		}
		const int size = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
		std::wstring result(static_cast<size_t>(size), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), result.data(), size);
		return result;
	}

	std::string GetExeDirectory()
	{
		WCHAR path[MAX_PATH]{};
		::GetModuleFileNameW(nullptr, path, MAX_PATH);
		return ToUtf8(std::filesystem::path(path).parent_path().wstring()) + "\\";
	}

	// 쿼터니언 → (pitch, yaw, roll) 라디안. quat::CreateFromYawPitchRoll의 역변환.
	vec3 ToEuler(const quat& q)
	{
		const f32 xx = q.x * q.x;
		const f32 yy = q.y * q.y;
		const f32 zz = q.z * q.z;

		const f32 m31 = 2.0f * q.x * q.z + 2.0f * q.y * q.w;
		const f32 m32 = 2.0f * q.y * q.z - 2.0f * q.x * q.w;
		const f32 m33 = 1.0f - 2.0f * xx - 2.0f * yy;

		const f32 cy = std::sqrt(m33 * m33 + m31 * m31);
		const f32 cx = std::atan2(-m32, cy);
		if (cy > 16.0f * FLT_EPSILON)
		{
			const f32 m12 = 2.0f * q.x * q.y + 2.0f * q.z * q.w;
			const f32 m22 = 1.0f - 2.0f * xx - 2.0f * zz;
			return vec3(cx, std::atan2(m31, m33), std::atan2(m12, m22));
		}

		const f32 m11 = 1.0f - 2.0f * yy - 2.0f * zz;
		const f32 m21 = 2.0f * q.x * q.y - 2.0f * q.z * q.w;
		return vec3(cx, 0.0f, std::atan2(-m21, m11));
	}

	// 표시용: 아주 작은 값과 -0을 0으로 (회전 변환 오차로 "-0.000"이 보이지 않도록)
	f32 CleanZero(f32 value)
	{
		return std::fabs(value) < 5e-4f ? 0.0f : value;
	}

	// 쿼터니언으로 방향 벡터 회전
	vec3 RotateVector(const vec3& v, const quat& q)
	{
		vec3 result;
		DirectX::XMStoreFloat3(&result, DirectX::XMVector3Rotate(DirectX::XMLoadFloat3(&v), DirectX::XMLoadFloat4(&q)));
		return result;
	}

	// 점 변환 (w로 나눔: 투영 역행렬에도 사용)
	vec3 TransformPoint(const vec3& v, const matx& m)
	{
		vec3 result;
		DirectX::XMStoreFloat3(&result, DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&v), m));
		return result;
	}

	bool IsTextureFile(const std::string& path)
	{
		const std::string ext = std::filesystem::path(ToWide(path)).extension().string();
		std::string lower;
		for (char c : ext)
		{
			lower.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
		}
		return lower == ".png" || lower == ".jpg" || lower == ".jpeg" || lower == ".bmp" || lower == ".gif" || lower == ".tif" || lower == ".tiff";
	}

	bool IsShaderFile(const std::string& path)
	{
		const std::string ext = std::filesystem::path(ToWide(path)).extension().string();
		return ext == ".hlsl" || ext == ".HLSL";
	}

	// 부모를 바꿔도 월드 위치/회전/크기가 유지되도록 로컬 값을 다시 계산한다
	void SetParentKeepWorld(Transform* child, Transform* parent)
	{
		const matx world = child->GetWorldMatrix();
		child->SetParent(parent);
		if (child->GetParent() != parent)
		{
			return; // 순환 등으로 거부됨
		}

		const matx local = parent != nullptr ? world * parent->GetWorldMatrix().Invert() : world;

		vec3 scale;
		quat rotation;
		vec3 position;
		if (matx(local).Decompose(scale, rotation, position))
		{
			child->SetPosition(position);
			child->SetRotation(rotation);
			child->SetScale(scale);
		}
	}
}
