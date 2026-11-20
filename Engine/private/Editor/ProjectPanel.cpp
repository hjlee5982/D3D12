#include "pch.h"
#include "ProjectPanel.h"
#include "EditorUtils.h"

#include "Engine.h"
#include "Log.h"
#include "RenderResources.h"
#include "ResourceManager.h"

#include "imgui.h"
#include "imgui_internal.h" // FindWindowSettingsByID (배치 파일에 이 창이 있는지)
#include "misc/cpp/imgui_stdlib.h"

#include <shellapi.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>

#pragma comment(lib, "shell32.lib")

// Windows.h의 min/max 매크로가 std::min과 지역 변수 이름을 가린다
#undef min
#undef max

using namespace EditorUtil;

namespace
{
	constexpr f32 kListThreshold = 40.0f; // 아이콘 크기가 이보다 작으면 목록 보기
	constexpr f32 kMinIconSize   = 32.0f;
	constexpr f32 kMaxIconSize   = 128.0f;
	constexpr u64 kRescanMs      = 1000;

	std::string ParentOf(const std::string& path)
	{
		const size_t slash = path.find_last_of('/');
		return slash == std::string::npos ? std::string() : path.substr(0, slash);
	}

	std::string Join(const std::string& folder, const std::string& name)
	{
		return folder.empty() ? name : folder + "/" + name;
	}

	// folder가 path 자신이거나 그 상위 폴더인가 ("" = Assets는 모든 경로의 상위)
	bool IsSameOrAncestor(const std::string& folder, const std::string& path)
	{
		if (folder.empty() || folder == path)
		{
			return true;
		}
		return path.size() > folder.size() && path.compare(0, folder.size(), folder) == 0 && path[folder.size()] == '/';
	}

	std::string ToLowerAscii(std::string text)
	{
		for (char& c : text)
		{
			if (c >= 'A' && c <= 'Z')
			{
				c = static_cast<char>(c - 'A' + 'a');
			}
		}
		return text;
	}

	std::string FormatSize(u64 bytes)
	{
		char buffer[32];
		if (bytes < 1024)               { std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes)); }
		else if (bytes < 1024ull * 1024) { std::snprintf(buffer, sizeof(buffer), "%.1f KB", bytes / 1024.0); }
		else                             { std::snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / (1024.0 * 1024.0)); }
		return buffer;
	}

	// 폭에 맞지 않으면 UTF-8 글자 단위로 잘라 "..."을 붙인다
	std::string FitText(const std::string& text, f32 maxWidth)
	{
		if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
		{
			return text;
		}

		std::string best = "...";
		size_t i = 0;
		while (i < text.size())
		{
			const unsigned char lead = static_cast<unsigned char>(text[i]);
			i += lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
			const std::string candidate = text.substr(0, std::min(i, text.size())) + "...";
			if (ImGui::CalcTextSize(candidate.c_str()).x > maxWidth)
			{
				break;
			}
			best = candidate;
		}
		return best;
	}

	// Windows 파일 이름 규칙
	bool IsValidFileName(const std::string& name)
	{
		if (name.empty() || name == "." || name == ".." || name.back() == ' ' || name.back() == '.')
		{
			return false;
		}
		return name.find_first_of("\\/:*?\"<>|") == std::string::npos;
	}

	ImU32 KindColor(int kind)
	{
		switch (kind)
		{
		case 1:  return IM_COL32(170, 120, 220, 255); // Texture
		case 2:  return IM_COL32( 80, 190, 175, 255); // Shader
		case 3:  return IM_COL32( 90, 140, 230, 255); // Scene
		default: return IM_COL32(130, 130, 130, 255);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// 스캔
// ---------------------------------------------------------------------------------------------

ProjectPanel::Kind ProjectPanel::GetKind(const std::string& name, bool isFolder)
{
	if (isFolder)
	{
		return Kind::Folder;
	}
	if (IsTextureFile(name))
	{
		return Kind::Texture;
	}
	if (IsShaderFile(name))
	{
		return Kind::Shader;
	}
	const std::string lower = ToLowerAscii(name);
	if (lower.size() > 6 && lower.compare(lower.size() - 6, 6, ".scene") == 0)
	{
		return Kind::Scene;
	}
	return Kind::Other;
}

std::wstring ProjectPanel::ToAbsolute(const std::string& path) const
{
	const ResourceManager* resources = _ctx.engine.GetResourceManager();
	std::wstring absolute = ToWide(resources != nullptr ? resources->GetAssetRoot() + path : path);
	std::replace(absolute.begin(), absolute.end(), L'/', L'\\');
	while (absolute.size() > 3 && absolute.back() == L'\\')
	{
		absolute.pop_back();
	}
	return absolute;
}

const ProjectPanel::Entry* ProjectPanel::FindEntry(const std::string& path) const
{
	for (const Entry& entry : _all)
	{
		if (entry.path == path)
		{
			return &entry;
		}
	}
	return nullptr;
}

void ProjectPanel::Rescan()
{
	_lastScan = ::GetTickCount64();
	_root = FolderNode{ "Assets", "", {} };
	_all.clear();
	_contents.clear();

	const ResourceManager* resources = _ctx.engine.GetResourceManager();
	if (resources == nullptr || resources->GetAssetRoot().empty())
	{
		return;
	}

	ScanFolder(ToAbsolute(""), "", _root);

	// 보던 폴더/선택이 (탐색기 등에서) 사라졌으면 남아 있는 가장 가까운 상위 폴더로
	while (!_currentFolder.empty())
	{
		const Entry* entry = FindEntry(_currentFolder);
		if (entry != nullptr && entry->kind == Kind::Folder)
		{
			break;
		}
		_currentFolder = ParentOf(_currentFolder);
	}
	if (!_selected.empty() && FindEntry(_selected) == nullptr)
	{
		_selected.clear();
	}

	for (const Entry& entry : _all)
	{
		if (ParentOf(entry.path) == _currentFolder)
		{
			_contents.push_back(entry);
		}
	}
}

void ProjectPanel::ScanFolder(const std::wstring& absolute, const std::string& relative, FolderNode& node)
{
	std::vector<Entry> folders;
	std::vector<Entry> files;

	std::error_code ec;
	for (auto it = std::filesystem::directory_iterator(absolute, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
	{
		const std::string name = ToUtf8(it->path().filename().wstring());
		if (name.empty() || name[0] == '.')
		{
			continue;
		}

		std::error_code statusError;
		const bool isFolder = it->is_directory(statusError);
		if (!isFolder && !it->is_regular_file(statusError))
		{
			continue;
		}

		Entry entry;
		entry.name = name;
		entry.path = Join(relative, name);
		entry.kind = GetKind(name, isFolder);
		entry.size = isFolder ? 0 : static_cast<u64>(it->file_size(statusError));
		(isFolder ? folders : files).push_back(std::move(entry));
	}

	// 폴더 먼저, 각각 이름순 (대소문자 무시)
	auto byName = [](const Entry& a, const Entry& b) { return ToLowerAscii(a.name) < ToLowerAscii(b.name); };
	std::sort(folders.begin(), folders.end(), byName);
	std::sort(files.begin(), files.end(), byName);

	for (const Entry& folder : folders)
	{
		_all.push_back(folder);
		node.children.push_back(FolderNode{ folder.name, folder.path, {} });
		ScanFolder(absolute + L"\\" + ToWide(folder.name), folder.path, node.children.back());
	}
	_all.insert(_all.end(), files.begin(), files.end());
}

// ---------------------------------------------------------------------------------------------
// 그리기
// ---------------------------------------------------------------------------------------------

void ProjectPanel::Draw(unsigned int assetsDockId)
{
	// 닫혀 있으면 아래의 SetNextWindow*가 다른 창에 적용되지 않도록 먼저 빠져나간다
	if (!_ctx.IsWindowOpen(EditorWindow::Project))
	{
		return;
	}

	// 처음 열리는 Project 창(이전 배치 파일에 없음)은 Assets 창 옆 탭으로 붙이고 앞으로 꺼낸다
	if (!_placed)
	{
		if (ImGui::FindWindowSettingsByID(ImHashStr("Project")) != nullptr)
		{
			_placed = true; // 저장된 배치가 있으면 그대로
		}
		else if (assetsDockId != 0)
		{
			ImGui::SetNextWindowDockID(assetsDockId, ImGuiCond_Always);
			_placed = true;
			BringToFront();
		}
	}
	else if (_bringToFront > 0)
	{
		// 탭 묶음은 도킹한 뒤 한두 프레임 지나야 생기고, 처음엔 먼저 만들어진 탭이 선택된다.
		// 그래서 이 창의 탭이 실제로 선택될 때까지 몇 프레임 동안 다시 시도한다.
		--_bringToFront;
		const ImGuiWindow* window = ImGui::FindWindowByName("Project");
		const ImGuiDockNode* node = window != nullptr ? window->DockNode : nullptr;
		if (node != nullptr && node->TabBar != nullptr)
		{
			if (node->TabBar->SelectedTabId == window->TabId)
			{
				_bringToFront = 0;
			}
			else
			{
				ImGui::SetWindowFocus("Project");
			}
		}
	}

	_ctx.PrepareWindow(EditorWindow::Project);
	const bool visible = ImGui::Begin("Project");
	_ctx.HandleWindowTitleClick(EditorWindow::Project);
	if (!visible)
	{
		ImGui::End();
		return;
	}

	const ResourceManager* resources = _ctx.engine.GetResourceManager();
	if (resources == nullptr || resources->GetAssetRoot().empty())
	{
		ImGui::TextDisabled("Assets folder not found");
		ImGui::End();
		return;
	}

	if (_lastScan == 0 || ::GetTickCount64() - _lastScan > kRescanMs)
	{
		Rescan();
	}

	DrawToolbar();

	const f32 footer = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

	ImGui::BeginChild("ProjectTree", ImVec2(200.0f, -footer), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
	DrawTree(_root);
	_revealCurrent = false;
	ImGui::EndChild();

	ImGui::SameLine();
	ImGui::BeginChild("ProjectContents", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders);
	DrawContents();
	ImGui::EndChild();

	DrawStatusBar();

	ApplyPending();
	DrawPopups();

	ImGui::End();
}

void ProjectPanel::DrawToolbar()
{
	// 경로: Assets > Scene > ...  (누르면 그 폴더로)
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, ImGui::GetStyle().ItemSpacing.y));

	std::vector<std::string> parts;
	for (std::string folder = _currentFolder; !folder.empty(); folder = ParentOf(folder))
	{
		parts.push_back(folder);
	}
	parts.push_back("");
	std::reverse(parts.begin(), parts.end());

	for (size_t i = 0; i < parts.size(); ++i)
	{
		if (i > 0)
		{
			ImGui::SameLine();
			ImGui::TextDisabled(">");
			ImGui::SameLine();
		}

		const std::string& folder = parts[i];
		const std::string label = folder.empty() ? "Assets" : folder.substr(folder.find_last_of('/') + 1);
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::Button(label.c_str()))
		{
			NavigateTo(folder);
		}
		ImGui::PopID();
	}

	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

	// 오른쪽: 검색, 아이콘 크기
	const f32 searchWidth = 180.0f;
	const f32 sliderWidth = 90.0f;
	const f32 rightWidth  = searchWidth + sliderWidth + ImGui::GetStyle().ItemSpacing.x;
	ImGui::SameLine();
	const f32 available = ImGui::GetContentRegionAvail().x;
	if (available > rightWidth)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - rightWidth);
	}

	ImGui::SetNextItemWidth(searchWidth);
	ImGui::InputTextWithHint("##search", "Search", &_search);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(sliderWidth);
	ImGui::SliderFloat("##iconSize", &_iconSize, kMinIconSize - 8.0f, kMaxIconSize, "");
	ImGui::SetItemTooltip("Icon size (smallest = list)");
}

void ProjectPanel::DrawTree(const FolderNode& node)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (node.children.empty())
	{
		flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (node.path == _currentFolder)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (node.path.empty())
	{
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}
	if (_revealCurrent && node.path != _currentFolder && IsSameOrAncestor(node.path, _currentFolder))
	{
		ImGui::SetNextItemOpen(true);
	}

	// 이름 앞에 아이콘 자리를 공백으로 비워 두고, 노드를 그린 뒤 그 자리에 폴더 아이콘을 그린다
	const f32 iconSize = ImGui::GetTextLineHeight();
	const int padding  = static_cast<int>(std::ceil((iconSize + 8.0f) / ImGui::CalcTextSize(" ").x));
	const std::string label = std::string(static_cast<size_t>(padding), ' ') + node.name;

	const bool open = ImGui::TreeNodeEx(node.path.empty() ? "<Assets>" : node.path.c_str(), flags, "%s", label.c_str());

	const ImVec2 rowMin = ImGui::GetItemRectMin();
	const f32 rowHeight = ImGui::GetItemRectSize().y;
	const Entry folderIcon{ node.name, node.path, Kind::Folder, 0 };
	DrawIcon(folderIcon, ImVec2(rowMin.x + ImGui::GetTreeNodeToLabelSpacing(), rowMin.y + (rowHeight - iconSize) * 0.5f), iconSize);

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		NavigateTo(node.path);
	}

	// 파일을 폴더 트리에 놓는 기능은 없으므로 우클릭 메뉴만
	if (ImGui::BeginPopupContextItem())
	{
		const Entry* entry = FindEntry(node.path);
		const Entry root{ "Assets", "", Kind::Folder, 0 };
		DrawEntryMenu(entry != nullptr ? *entry : root);
		ImGui::EndPopup();
	}

	if (open)
	{
		for (const FolderNode& child : node.children)
		{
			DrawTree(child);
		}
		ImGui::TreePop();
	}
}

void ProjectPanel::DrawContents()
{
	// 검색어가 있으면 Assets 전체에서 이름으로 찾는다
	std::vector<Entry> results;
	const std::vector<Entry>* items = &_contents;
	if (!_search.empty())
	{
		const std::string needle = ToLowerAscii(_search);
		for (const Entry& entry : _all)
		{
			if (ToLowerAscii(entry.name).find(needle) != std::string::npos)
			{
				results.push_back(entry);
			}
		}
		items = &results;
	}

	if (items->empty())
	{
		ImGui::TextDisabled(_search.empty() ? "This folder is empty" : "No results");
	}

	if (_iconSize < kListThreshold)
	{
		for (const Entry& entry : *items)
		{
			DrawListItem(entry);
		}
	}
	else
	{
		const f32 spacing   = ImGui::GetStyle().ItemSpacing.x;
		const f32 cellWidth = _iconSize + 16.0f;
		const int columns   = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + spacing) / (cellWidth + spacing)));

		for (size_t i = 0; i < items->size(); ++i)
		{
			if (i % columns != 0)
			{
				ImGui::SameLine();
			}
			DrawGridItem((*items)[i], cellWidth);
		}
	}

	// 빈 곳 클릭: 선택 해제, 우클릭: 현재 폴더 메뉴
	if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		_selected.clear();
	}
	if (ImGui::BeginPopupContextWindow("FolderMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::MenuItem("Create Folder"))
		{
			_pendingCreateIn  = _currentFolder;
			_hasPendingCreate = true;
		}
		if (ImGui::MenuItem("Show in Explorer"))
		{
			ShowInExplorer(_currentFolder);
		}
		if (ImGui::MenuItem("Refresh"))
		{
			_lastScan = 0;
		}
		ImGui::EndPopup();
	}
}

void ProjectPanel::DrawGridItem(const Entry& entry, f32 cellWidth)
{
	ImGui::PushID(entry.path.c_str());

	const f32 lineHeight = ImGui::GetTextLineHeight();
	ImGui::InvisibleButton("##item", ImVec2(cellWidth, _iconSize + lineHeight + 10.0f));
	HandleItemInteraction(entry);

	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	if (entry.path == _selected)
	{
		drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_Header), 4.0f);
	}
	else if (ImGui::IsItemHovered())
	{
		drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.35f), 4.0f);
	}

	DrawIcon(entry, ImVec2(min.x + (cellWidth - _iconSize) * 0.5f, min.y + 4.0f), _iconSize);

	const std::string label = FitText(entry.name, cellWidth - 6.0f);
	const f32 textWidth = ImGui::CalcTextSize(label.c_str()).x;
	drawList->AddText(ImVec2(min.x + (cellWidth - textWidth) * 0.5f, min.y + _iconSize + 7.0f), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());

	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		ImGui::SetTooltip("%s", entry.path.c_str());
	}

	ImGui::PopID();
}

void ProjectPanel::DrawListItem(const Entry& entry)
{
	ImGui::PushID(entry.path.c_str());

	const f32 height = ImGui::GetTextLineHeight() + 2.0f;
	ImGui::Selectable("##row", entry.path == _selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, height));
	HandleItemInteraction(entry);

	const ImVec2 min = ImGui::GetItemRectMin();
	DrawIcon(entry, ImVec2(min.x + 2.0f, min.y + 1.0f), height - 2.0f);

	// 검색 중에는 어느 폴더의 파일인지 알 수 있도록 경로를 보여 준다
	const std::string& text = _search.empty() ? entry.name : entry.path;
	ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + height + 6.0f, min.y + 1.0f), ImGui::GetColorU32(ImGuiCol_Text), text.c_str());

	ImGui::PopID();
}

void ProjectPanel::DrawIcon(const Entry& entry, const ImVec2& min, f32 size) const
{
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 max(min.x + size, min.y + size);

	if (entry.kind == Kind::Folder)
	{
		const ImU32 back  = IM_COL32(200, 150, 60, 255);
		const ImU32 front = IM_COL32(235, 185, 80, 255);
		const f32 rounding = size * 0.06f;
		drawList->AddRectFilled(ImVec2(min.x + size * 0.06f, min.y + size * 0.16f), ImVec2(min.x + size * 0.45f, min.y + size * 0.32f), back, rounding);
		drawList->AddRectFilled(ImVec2(min.x + size * 0.06f, min.y + size * 0.24f), ImVec2(max.x - size * 0.06f, max.y - size * 0.12f), back, rounding);
		drawList->AddRectFilled(ImVec2(min.x + size * 0.06f, min.y + size * 0.34f), ImVec2(max.x - size * 0.06f, max.y - size * 0.12f), front, rounding);
		return;
	}

	// 로드된 텍스처는 미리 보기 (비율 유지)
	if (entry.kind == Kind::Texture)
	{
		const ResourceManager* resources = _ctx.engine.GetResourceManager();
		const Texture* texture = resources != nullptr ? resources->Get(resources->FindTexture(entry.path)) : nullptr;
		if (texture != nullptr && texture->srv != nullptr && texture->width > 0 && texture->height > 0)
		{
			const f32 aspect = static_cast<f32>(texture->width) / static_cast<f32>(texture->height);
			const f32 w = aspect >= 1.0f ? size : size * aspect;
			const f32 h = aspect >= 1.0f ? size / aspect : size;
			const ImVec2 imageMin(min.x + (size - w) * 0.5f, min.y + (size - h) * 0.5f);
			const ImVec2 imageMax(imageMin.x + w, imageMin.y + h);
			drawList->AddImage(reinterpret_cast<ImTextureID>(texture->srv.Get()), imageMin, imageMax);
			drawList->AddRect(imageMin, imageMax, IM_COL32(0, 0, 0, 120));
			return;
		}
	}

	// 문서 모양: 접힌 귀퉁이 + 종류별 색 띠 + 확장자
	const f32 x0 = min.x + size * 0.16f;
	const f32 x1 = max.x - size * 0.16f;
	const f32 y0 = min.y + size * 0.04f;
	const f32 y1 = max.y - size * 0.04f;
	const f32 fold = size * 0.2f;

	const ImVec2 page[5] = { ImVec2(x0, y0), ImVec2(x1 - fold, y0), ImVec2(x1, y0 + fold), ImVec2(x1, y1), ImVec2(x0, y1) };
	drawList->AddConvexPolyFilled(page, 5, IM_COL32(215, 215, 215, 255));
	drawList->AddTriangleFilled(ImVec2(x1 - fold, y0), ImVec2(x1 - fold, y0 + fold), ImVec2(x1, y0 + fold), IM_COL32(160, 160, 160, 255));

	const ImU32 color = KindColor(static_cast<int>(entry.kind));
	const f32 bandTop = y1 - size * 0.38f;
	const f32 bandBottom = y1 - size * 0.12f;
	drawList->AddRectFilled(ImVec2(x0, bandTop), ImVec2(x1, bandBottom), color);

	if (size >= 40.0f)
	{
		const size_t dot = entry.name.find_last_of('.');
		std::string extension = dot == std::string::npos ? std::string() : entry.name.substr(dot + 1);
		for (char& c : extension)
		{
			c = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
		}
		if (extension.size() > 5)
		{
			extension.resize(5);
		}

		ImFont* font = ImGui::GetFont();
		const f32 fontSize = std::min(ImGui::GetFontSize(), (bandBottom - bandTop) * 0.85f);
		const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, extension.c_str());
		drawList->AddText(font, fontSize,
			ImVec2((x0 + x1 - textSize.x) * 0.5f, (bandTop + bandBottom - textSize.y) * 0.5f),
			IM_COL32(255, 255, 255, 255), extension.c_str());
	}
}

void ProjectPanel::HandleItemInteraction(const Entry& entry)
{
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		_selected = entry.path;
	}
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		Open(entry);
	}

	// 파일 끌기: Inspector/Assets 창의 텍스처·셰이더 칸에 놓을 수 있다
	if (entry.kind != Kind::Folder && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(kAssetPayload, entry.path.c_str(), entry.path.size() + 1);
		ImGui::TextUnformatted(entry.path.c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginPopupContextItem("EntryMenu"))
	{
		DrawEntryMenu(entry);
		ImGui::EndPopup();
	}
}

void ProjectPanel::DrawEntryMenu(const Entry& entry)
{
	ResourceManager* resources = _ctx.engine.GetResourceManager();

	ImGui::TextDisabled("%s", entry.path.empty() ? "Assets" : entry.path.c_str());
	ImGui::Separator();

	switch (entry.kind)
	{
	case Kind::Folder:
		if (ImGui::MenuItem("Open"))
		{
			NavigateTo(entry.path);
		}
		if (ImGui::MenuItem("Create Folder"))
		{
			_pendingCreateIn  = entry.path;
			_hasPendingCreate = true;
		}
		break;

	case Kind::Scene:
		if (ImGui::MenuItem("Open Scene"))
		{
			_ctx.pendingLoad = entry.path;
		}
		break;

	case Kind::Texture:
	case Kind::Shader:
	{
		const bool isTexture = entry.kind == Kind::Texture;
		const TextureHandle texture = isTexture ? resources->FindTexture(entry.path) : TextureHandle{};
		const ShaderHandle  shader  = isTexture ? ShaderHandle{} : resources->FindShader(entry.path);
		const bool loaded = isTexture ? resources->IsAlive(texture) : resources->IsAlive(shader);

		if (!loaded)
		{
			if (ImGui::MenuItem("Load"))
			{
				if (isTexture) { resources->LoadTexture(entry.path); }
				else           { resources->LoadShader(entry.path); }
			}
		}
		else
		{
			if (ImGui::MenuItem("Reload"))
			{
				if (isTexture) { resources->ReloadTexture(texture); }
				else           { resources->ReloadShader(shader); }
			}
			// 이 리소스를 쓰는 머티리얼/렌더러의 핸들은 무효가 된다 (크래시 없이 표시만 사라짐)
			if (ImGui::MenuItem("Unload"))
			{
				if (isTexture) { resources->Destroy(texture); }
				else           { resources->Destroy(shader); }
			}
		}
		break;
	}

	default:
		break;
	}

	if (entry.kind != Kind::Folder && ImGui::MenuItem("Open in External Program"))
	{
		::ShellExecuteW(nullptr, L"open", ToAbsolute(entry.path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	ImGui::Separator();
	if (ImGui::MenuItem("Show in Explorer"))
	{
		ShowInExplorer(entry.path);
	}

	const std::string lock = GetLockReason(entry.path);
	ImGui::BeginDisabled(!lock.empty());
	if (ImGui::MenuItem("Rename"))
	{
		_renameTarget = entry.path;
		_renameBuffer = entry.name;
		_openRename   = true;
	}
	if (ImGui::MenuItem("Delete"))
	{
		_deleteTarget = entry.path;
		_openDelete   = true;
	}
	ImGui::EndDisabled();
	if (!lock.empty())
	{
		ImGui::TextDisabled("%s", lock.c_str());
	}
}

void ProjectPanel::DrawStatusBar()
{
	if (!_error.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", _error.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("x"))
		{
			_error.clear();
		}
		return;
	}

	const Entry* entry = _selected.empty() ? nullptr : FindEntry(_selected);
	if (entry == nullptr)
	{
		ImGui::TextDisabled("Assets/%s  (%d items)", _currentFolder.c_str(), static_cast<int>(_contents.size()));
		return;
	}

	ImGui::TextUnformatted(("Assets/" + entry->path).c_str());
	if (entry->kind == Kind::Folder)
	{
		return;
	}

	ImGui::SameLine();
	ImGui::TextDisabled("| %s", FormatSize(entry->size).c_str());

	const ResourceManager* resources = _ctx.engine.GetResourceManager();
	if (entry->kind == Kind::Texture)
	{
		if (const Texture* texture = resources->Get(resources->FindTexture(entry->path)))
		{
			ImGui::SameLine();
			ImGui::TextDisabled("| %ux%u", texture->width, texture->height);
		}
	}
	if (entry->kind == Kind::Texture || entry->kind == Kind::Shader)
	{
		const bool loaded = entry->kind == Kind::Texture ? resources->IsAlive(resources->FindTexture(entry->path))
		                                                 : resources->IsAlive(resources->FindShader(entry->path));
		ImGui::SameLine();
		if (loaded) { ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "| loaded"); }
		else        { ImGui::TextDisabled("| not loaded"); }
	}
	if (entry->kind == Kind::Scene && entry->path == _ctx.engine.GetScenePath())
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "| open");
	}
}

void ProjectPanel::DrawPopups()
{
	if (_openRename)
	{
		ImGui::OpenPopup("Rename Asset");
		_openRename = false;
	}
	if (_openDelete)
	{
		ImGui::OpenPopup("Delete Asset");
		_openDelete = false;
	}

	if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextDisabled("Assets/%s", _renameTarget.c_str());
		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::SetNextItemWidth(300.0f);
		const bool submitted = ImGui::InputText("##name", &_renameBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

		const bool valid = IsValidFileName(_renameBuffer);
		if (!valid)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Invalid name");
		}
		else if (!_error.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", _error.c_str());
		}

		ImGui::BeginDisabled(!valid);
		if ((ImGui::Button("Rename") || (submitted && valid)) && ApplyRename())
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupModal("Delete Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		const Entry* entry = FindEntry(_deleteTarget);
		ImGui::Text("Move to the Recycle Bin?");
		ImGui::TextDisabled("Assets/%s", _deleteTarget.c_str());
		if (entry != nullptr && entry->kind == Kind::Folder)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Everything inside the folder is moved too.");
		}

		if (ImGui::Button("Delete"))
		{
			DeleteToRecycleBin(_deleteTarget);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		// (Esc는 텍스트 입력 중이 아니면 호스트가 받아 프로그램을 끝내므로 여기서는 쓰지 않는다)
		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

// ---------------------------------------------------------------------------------------------
// 동작
// ---------------------------------------------------------------------------------------------

void ProjectPanel::ApplyPending()
{
	if (_hasPendingNavigate)
	{
		_hasPendingNavigate = false;
		_currentFolder = _pendingNavigate;
		_selected.clear();
		_search.clear();
		_revealCurrent = true;
		Rescan();
	}
	if (_hasPendingCreate)
	{
		_hasPendingCreate = false;
		CreateFolder(_pendingCreateIn);
	}
}

void ProjectPanel::Open(const Entry& entry)
{
	switch (entry.kind)
	{
	case Kind::Folder:
		NavigateTo(entry.path);
		break;

	case Kind::Scene:
		_ctx.pendingLoad = entry.path; // 재생 중이면 재생을 끝내고 연다 (EditorContext)
		break;

	default:
		::ShellExecuteW(nullptr, L"open", ToAbsolute(entry.path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		break;
	}
}

void ProjectPanel::CreateFolder(const std::string& parent)
{
	_error.clear();

	std::string path = Join(parent, "New Folder");
	for (int i = 1; std::filesystem::exists(ToAbsolute(path)); ++i)
	{
		path = Join(parent, "New Folder " + std::to_string(i));
	}

	std::error_code ec;
	if (!std::filesystem::create_directory(ToAbsolute(path), ec))
	{
		_error = "Could not create folder (error " + std::to_string(ec.value()) + ")";
		return;
	}
	EngineLog("[Editor] Created folder 'Assets/%s'", path.c_str());

	// 만든 폴더가 보이도록 그 폴더의 상위로 이동하고, 바로 이름을 바꿀 수 있게 한다 (유니티와 같은 흐름)
	_currentFolder = parent;
	_revealCurrent = true;
	_search.clear();
	Rescan();
	_selected     = path;
	_renameTarget = path;
	_renameBuffer = path.substr(path.find_last_of('/') + 1);
	_openRename   = true;
}

bool ProjectPanel::ApplyRename()
{
	_error.clear();

	const std::string target  = _renameTarget;
	const std::string newPath = Join(ParentOf(target), _renameBuffer);
	if (newPath == target)
	{
		return true;
	}

	const std::string lock = GetLockReason(target);
	if (!lock.empty())
	{
		_error = lock;
		return true;
	}

	// 대소문자만 바꾸는 경우는 Windows에서 같은 파일이므로 존재 검사에서 뺀다
	if (ToLowerAscii(newPath) != ToLowerAscii(target) && std::filesystem::exists(ToAbsolute(newPath)))
	{
		_error = "'" + _renameBuffer + "' already exists";
		return false;
	}

	std::error_code ec;
	std::filesystem::rename(ToAbsolute(target), ToAbsolute(newPath), ec);
	if (ec)
	{
		_error = "Rename failed (error " + std::to_string(ec.value()) + ")";
		return true;
	}
	EngineLog("[Editor] Renamed 'Assets/%s' -> 'Assets/%s'", target.c_str(), newPath.c_str());

	// 이름을 바꾼 폴더 안을 보고 있었으면 경로를 따라간다
	if (IsSameOrAncestor(target, _currentFolder) && !target.empty())
	{
		_currentFolder = newPath + _currentFolder.substr(target.size());
	}
	_selected = newPath;
	Rescan();
	return true;
}

bool ProjectPanel::DeleteToRecycleBin(const std::string& path)
{
	_error.clear();

	const std::string lock = GetLockReason(path);
	if (!lock.empty())
	{
		_error = lock;
		return false;
	}

	// SHFileOperation은 이중 널로 끝나는 목록을 받는다. FOF_ALLOWUNDO = 휴지통으로 (복구 가능)
	std::wstring from = ToAbsolute(path);
	from.push_back(L'\0');

	SHFILEOPSTRUCTW operation{};
	operation.wFunc  = FO_DELETE;
	operation.pFrom  = from.c_str();
	operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;

	const int result = ::SHFileOperationW(&operation);
	if (result != 0 || operation.fAnyOperationsAborted)
	{
		_error = "Delete failed (code " + std::to_string(result) + ")";
		Rescan();
		return false;
	}
	EngineLog("[Editor] Moved 'Assets/%s' to the Recycle Bin", path.c_str());

	if (IsSameOrAncestor(path, _currentFolder))
	{
		_currentFolder = ParentOf(path);
	}
	_selected.clear();
	Rescan();
	return true;
}

void ProjectPanel::ShowInExplorer(const std::string& path) const
{
	const std::wstring absolute = ToAbsolute(path);
	if (path.empty())
	{
		::ShellExecuteW(nullptr, L"open", absolute.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		return;
	}

	const std::wstring parameters = L"/select,\"" + absolute + L"\"";
	::ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL);
}

std::string ProjectPanel::GetLockReason(const std::string& path) const
{
	if (path.empty())
	{
		return "The Assets folder itself cannot be changed";
	}

	const std::string& scenePath = _ctx.engine.GetScenePath();
	if (!scenePath.empty() && IsSameOrAncestor(path, scenePath))
	{
		return "In use: open scene '" + scenePath + "'";
	}

	// 로드된 리소스는 경로로 파일 감시/리로드를 하므로, 경로가 바뀌면 연결이 끊긴다
	const ResourceManager* resources = _ctx.engine.GetResourceManager();
	if (resources != nullptr)
	{
		for (const auto& texture : resources->ListTextures())
		{
			if (IsSameOrAncestor(path, texture.name))
			{
				return "In use: loaded texture '" + texture.name + "' (unload first)";
			}
		}
		for (const auto& shader : resources->ListShaders())
		{
			if (IsSameOrAncestor(path, shader.name))
			{
				return "In use: loaded shader '" + shader.name + "' (unload first)";
			}
		}
	}
	return {};
}
