#pragma once

#include "EditorContext.h"

#include "imgui.h"

#include <string>
#include <vector>

// Project 창 (유니티의 Project 창): Assets 폴더를 최상위로 한 폴더 트리 + 선택한 폴더의 내용.
//
//   왼쪽  : 폴더 트리. 클릭하면 그 폴더의 내용을 오른쪽에 보여 준다.
//   오른쪽: 하위 폴더와 파일 (아이콘 격자, 크기 슬라이더를 끝까지 줄이면 목록)
//           더블클릭: 폴더 → 들어가기, .scene → 씬 열기, 그 밖의 파일 → 기본 프로그램으로 열기
//           끌어다 놓기: 텍스처/셰이더 파일을 Inspector·Assets 창의 해당 칸에 놓으면 로드해서 지정
//   우클릭: 새 폴더, 이름 바꾸기, 삭제(휴지통), 탐색기에서 보기, 로드/리로드/언로드
//
// 경로는 모두 Assets 기준 상대 경로 (UTF-8, '/'). 최상위(Assets)는 빈 문자열.
// 파일 목록은 1초마다 다시 읽어서 탐색기에서 바꾼 내용도 반영한다.
class ProjectPanel
{
public:
	explicit ProjectPanel(EditorContext& context) : _ctx(context) {}

	// assetsDockId: 처음 열릴 때(이전 배치 파일에 없을 때) Assets 창과 같은 곳에 탭으로 붙인다
	void Draw(unsigned int assetsDockId);

	// 같은 탭 묶음에서 Project 탭을 앞으로 (기본 배치를 새로 만들었을 때 Editor가 호출)
	void BringToFront() { _bringToFront = 10; }

private:
	enum class Kind : u8 { Folder, Texture, Shader, Scene, Other };

	struct Entry
	{
		std::string name; // 파일/폴더 이름
		std::string path; // Assets 기준 상대 경로
		Kind kind = Kind::Other;
		u64  size = 0;
	};

	struct FolderNode
	{
		std::string name;
		std::string path;
		std::vector<FolderNode> children;
	};

	void Rescan();
	void ScanFolder(const std::wstring& absolute, const std::string& relative, FolderNode& node);

	void DrawToolbar();
	void DrawTree(const FolderNode& node);
	void DrawContents();
	void DrawGridItem(const Entry& entry, f32 cellWidth);
	void DrawListItem(const Entry& entry);
	void DrawIcon(const Entry& entry, const ImVec2& min, f32 size) const;
	void HandleItemInteraction(const Entry& entry);
	void DrawEntryMenu(const Entry& entry);
	void DrawStatusBar();
	void DrawPopups();

	// 목록을 순회하는 도중에 다시 읽으면 순회 중인 항목이 사라지므로, 폴더 이동/새 폴더는
	// 요청만 기록하고 창을 다 그린 뒤 ApplyPending()에서 처리한다.
	void ApplyPending();
	void Open(const Entry& entry);
	void NavigateTo(const std::string& folder) { _pendingNavigate = folder; _hasPendingNavigate = true; }
	void CreateFolder(const std::string& parent);
	void BeginRename(const std::string& path);
	bool ApplyRename();
	bool DeleteToRecycleBin(const std::string& path);
	void ShowInExplorer(const std::string& path) const;

	// 사용 중(로드된 텍스처/셰이더, 열린 씬)이면 이름 바꾸기/삭제를 막는다. 이유를 돌려준다.
	std::string GetLockReason(const std::string& path) const;

	std::wstring ToAbsolute(const std::string& path) const;
	const Entry* FindEntry(const std::string& path) const;
	static Kind  GetKind(const std::string& name, bool isFolder);

private:
	EditorContext& _ctx;

	FolderNode         _root;
	std::vector<Entry> _all;      // Assets 아래 모든 폴더/파일 (검색과 조회용)
	std::vector<Entry> _contents; // 현재 폴더의 직속 항목 (폴더 먼저, 이름순)
	u64 _lastScan = 0;

	std::string _currentFolder;   // "" = Assets
	std::string _selected;
	std::string _search;
	f32  _iconSize      = 72.0f;  // 이 값보다 작으면 목록 보기
	bool _revealCurrent = false;  // 트리에서 현재 폴더까지 펼치기
	bool _placed        = false;  // 첫 배치 처리 여부
	int  _bringToFront  = 0;      // 남은 시도 프레임 수 (탭을 앞으로 꺼내기)

	// 미뤄 둔 요청
	std::string _pendingNavigate;
	bool        _hasPendingNavigate = false;
	std::string _pendingCreateIn;
	bool        _hasPendingCreate   = false;

	// 팝업
	bool        _openRename = false;
	std::string _renameTarget;
	std::string _renameBuffer;
	bool        _openDelete = false;
	std::string _deleteTarget;
	std::string _error;
};
