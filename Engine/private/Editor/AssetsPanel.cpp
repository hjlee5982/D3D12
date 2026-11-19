#include "pch.h"
#include "AssetsPanel.h"
#include "EditorUtils.h"

#include "Camera.h"
#include "ComponentRegistry.h"
#include "Engine.h"
#include "GameObject.h"
#include "Log.h"
#include "MeshRenderer.h"
#include "RenderResources.h"
#include "ResourceManager.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Transform.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace EditorUtil;

void AssetsPanel::Draw()
{
	if (!ImGui::Begin("Assets"))
	{
		ImGui::End();
		return;
	}

	ResourceManager* resources = _ctx.engine.GetResourceManager();
	if (resources == nullptr)
	{
		ImGui::TextDisabled("No resource manager (D3D11 only)");
		ImGui::End();
		return;
	}

	bool watch = resources->IsFileWatchEnabled();
	if (ImGui::Checkbox("Hot reload on file change", &watch))
	{
		resources->SetFileWatchEnabled(watch);
	}

	const ResourceManager::Stats stats = resources->GetStats();
	ImGui::TextDisabled("meshes %u  textures %u  shaders %u  materials %u", stats.meshes, stats.textures, stats.shaders, stats.materials);

	if (ImGui::BeginTabBar("AssetTabs"))
	{
		if (ImGui::BeginTabItem("Files"))
		{
			DrawFiles();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Materials"))
		{
			DrawMaterials();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Meshes"))
		{
			for (const auto& mesh : resources->ListMeshes())
			{
				ImGui::BulletText("%s", mesh.name.c_str());
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
}

void AssetsPanel::DrawFiles()
{
	ResourceManager* resources = _ctx.engine.GetResourceManager();

	// 새 파일을 반영하도록 2초마다 다시 스캔
	if (_lastScan == 0 || ::GetTickCount64() - _lastScan > 2000)
	{
		ScanFiles();
	}

	if (!ImGui::BeginTable("AssetFiles", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
	{
		return;
	}

	ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 70.0f);
	ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 130.0f);
	ImGui::TableHeadersRow();

	for (const std::string& path : _files)
	{
		ImGui::PushID(path.c_str());
		ImGui::TableNextRow();

		const bool isTexture = IsTextureFile(path);
		const TextureHandle texture = isTexture ? resources->FindTexture(path) : TextureHandle{};
		const ShaderHandle  shader  = isTexture ? ShaderHandle{} : resources->FindShader(path);
		const bool loaded = isTexture ? resources->IsAlive(texture) : resources->IsAlive(shader);

		ImGui::TableNextColumn();
		ImGui::TextUnformatted(path.c_str());

		ImGui::TableNextColumn();
		if (loaded)
		{
			ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "loaded");
		}
		else
		{
			ImGui::TextDisabled("-");
		}

		ImGui::TableNextColumn();
		if (!loaded)
		{
			if (ImGui::SmallButton("Load"))
			{
				if (isTexture) { resources->LoadTexture(path); }
				else           { resources->LoadShader(path); }
			}
		}
		else
		{
			if (ImGui::SmallButton("Reload"))
			{
				if (isTexture) { resources->ReloadTexture(texture); }
				else           { resources->ReloadShader(shader); }
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Unload"))
			{
				// 이 리소스를 쓰는 머티리얼/렌더러의 핸들은 무효가 된다 (크래시 없이 표시만 사라짐)
				if (isTexture) { resources->Destroy(texture); }
				else           { resources->Destroy(shader); }
			}
		}

		ImGui::PopID();
	}

	ImGui::EndTable();
}

void AssetsPanel::ScanFiles()
{
	_files.clear();
	_lastScan = ::GetTickCount64();

	ResourceManager* resources = _ctx.engine.GetResourceManager();
	if (resources == nullptr || resources->GetAssetRoot().empty())
	{
		return;
	}

	const std::filesystem::path root = ToWide(resources->GetAssetRoot());
	std::error_code ec;
	for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
	{
		if (!it->is_regular_file(ec))
		{
			continue;
		}

		std::wstring relative = it->path().lexically_relative(root).wstring();
		for (wchar_t& c : relative)
		{
			c = c == L'\\' ? L'/' : c;
		}

		const std::string path = ToUtf8(relative);
		if (IsTextureFile(path) || IsShaderFile(path))
		{
			_files.push_back(path);
		}
	}
	std::sort(_files.begin(), _files.end());
}

void AssetsPanel::DrawMaterials()
{
	ResourceManager* resources = _ctx.engine.GetResourceManager();

	ImGui::SetNextItemWidth(-70.0f);
	ImGui::InputText("##newMaterial", &_newMaterialName);
	ImGui::SameLine();
	ImGui::BeginDisabled(_newMaterialName.empty() || resources->FindMaterial(_newMaterialName).IsValid());
	if (ImGui::Button("Create"))
	{
		const auto shaders = resources->ListShaders();
		_ctx.selectedMaterial = resources->CreateMaterial(_newMaterialName, shaders.empty() ? ShaderHandle{} : shaders.front().handle, TextureHandle{});
		EngineLog("[Editor] Created material '%s'", _newMaterialName.c_str());
	}
	ImGui::EndDisabled();
	ImGui::Separator();

	const auto materials = resources->ListMaterials();

	ImGui::BeginChild("MaterialList", ImVec2(140.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
	for (const auto& material : materials)
	{
		ImGui::PushID(static_cast<int>(material.handle.index));
		if (ImGui::Selectable(material.name.c_str(), material.handle == _ctx.selectedMaterial))
		{
			_ctx.selectedMaterial = material.handle;
		}
		ImGui::PopID();
	}
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("MaterialDetails");
	if (!resources->IsAlive(_ctx.selectedMaterial))
	{
		_ctx.selectedMaterial = {};
		ImGui::TextDisabled("Select a material");
	}
	else
	{
		ShaderHandle shader = resources->GetMaterialShader(_ctx.selectedMaterial);
		if (ResourceCombo("Shader", shader, resources->ListShaders(), resources->IsAlive(shader)))
		{
			resources->SetMaterialShader(_ctx.selectedMaterial, shader);
		}

		TextureHandle texture = resources->GetMaterialTexture(_ctx.selectedMaterial);
		if (ResourceCombo("Texture", texture, resources->ListTextures(), resources->IsAlive(texture)))
		{
			resources->SetMaterialTexture(_ctx.selectedMaterial, texture);
		}

		ImGui::Spacing();
		if (ImGui::Button("Delete material"))
		{
			resources->Destroy(_ctx.selectedMaterial);
			_ctx.selectedMaterial = {};
		}
	}
	ImGui::EndChild();
}
