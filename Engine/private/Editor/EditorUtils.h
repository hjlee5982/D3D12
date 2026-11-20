#pragma once

// 에디터 패널들이 함께 쓰는 도우미 (Engine 내부 전용)

#include "ResourceManager.h"
#include "Types.h"

#include "imgui.h"

#include <string>
#include <vector>

class Transform;

namespace EditorUtil
{
	// Hierarchy → Inspector 등으로 오브젝트를 끌어다 놓을 때의 페이로드 이름 (ObjectId)
	inline constexpr const char* kObjectPayload = "D3D12_OBJECT";
	// Project 창에서 파일을 끌 때의 페이로드 이름 (Assets 기준 상대 경로, 널 종료 UTF-8)
	inline constexpr const char* kAssetPayload = "D3D12_ASSET";

	std::string  ToUtf8(const std::wstring& wide);
	std::wstring ToWide(const std::string& utf8);
	std::string  GetExeDirectory(); // 끝에 구분자 포함

	vec3 ToEuler(const quat& q);  // (pitch, yaw, roll) 라디안. quat::CreateFromYawPitchRoll의 역변환
	f32  CleanZero(f32 value);    // 표시용: -0과 아주 작은 값을 0으로
	vec3 RotateVector(const vec3& v, const quat& q);
	vec3 TransformPoint(const vec3& v, const matx& m); // w로 나눔 (투영 역행렬에도 사용)

	bool IsTextureFile(const std::string& path);
	bool IsShaderFile(const std::string& path);

	// 부모를 바꿔도 월드 위치/회전/크기가 유지되도록 로컬 값을 다시 계산한다
	void SetParentKeepWorld(Transform* child, Transform* parent);

	// 바로 앞 항목에 Project 창의 텍스처/셰이더 파일을 놓으면 로드해서 handle에 넣는다. 바뀌었으면 true.
	bool AcceptAssetDrop(ResourceManager* resources, TextureHandle& handle);
	bool AcceptAssetDrop(ResourceManager* resources, ShaderHandle& handle);

	// 리소스 핸들 콤보: (none) + 목록. 핸들이 가리키는 리소스가 삭제됐으면 (missing)으로 표시.
	template <typename H>
	bool ResourceCombo(const char* label, H& handle, const std::vector<ResourceEntry<H>>& entries, bool alive)
	{
		std::string preview = "(none)";
		if (handle.IsValid())
		{
			preview = "(missing)";
			if (alive)
			{
				for (const auto& entry : entries)
				{
					if (entry.handle == handle)
					{
						preview = entry.name;
						break;
					}
				}
			}
		}

		bool changed = false;
		if (ImGui::BeginCombo(label, preview.c_str()))
		{
			if (ImGui::Selectable("(none)", !handle.IsValid()))
			{
				handle  = H{};
				changed = true;
			}
			for (const auto& entry : entries)
			{
				ImGui::PushID(static_cast<int>(entry.handle.index));
				if (ImGui::Selectable(entry.name.c_str(), entry.handle == handle))
				{
					handle  = entry.handle;
					changed = true;
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		return changed;
	}
}
