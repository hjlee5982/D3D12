#pragma once

#include "Handle.h"
#include "Types.h"

#include <string>

class Engine;
class GameObject;
class Scene;

// 에디터 창 (Window 메뉴에 이 순서로 나온다)
enum class EditorWindow : u8 { Scene, Game, Hierarchy, Inspector, Project, Assets, Count };

// 에디터 패널들이 함께 쓰는 상태 (Engine 내부 전용).
//
// 패널은 자기 화면 상태만 갖고, 여러 패널이 보는 값(선택 등)과 씬을 바꾸는 요청은 여기에 둔다.
// 씬 변경(생성/삭제/부모 변경/씬 열기)은 패널이 오브젝트 목록을 순회하는 도중에 하면 안 되므로
// 요청만 기록해 두고, 모든 패널을 그린 뒤 ApplyPendingEdits()에서 한꺼번에 처리한다.
struct EditorContext
{
	explicit EditorContext(Engine& engine) : engine(engine) {}

	Engine&              engine;
	ID3D11Device*        device        = nullptr;
	ID3D11DeviceContext* deviceContext = nullptr;

	// 선택 (삭제되면 Get()이 nullptr → 자동으로 선택 해제)
	ObjectRef      selected;
	MaterialHandle selectedMaterial; // Assets 패널에서 고른 머티리얼 (새 Quad에 사용)

	// 보기
	bool showDemo    = false;
	bool resetLayout = false;

	// --- 창 열기/닫기 ---
	// 닫힌 창은 Begin을 부르지 않는다. ImGui는 창의 위치·도킹 정보를 계속 갖고 있으므로 다시 열면 원래 자리에 나온다.
	// 닫힌 창 목록은 imgui.ini에 함께 저장한다 (Editor가 등록하는 설정 처리기).
	static const char* GetWindowName(EditorWindow window);
	bool IsWindowOpen(EditorWindow window) const { return windowOpen[static_cast<int>(window)]; }
	void OpenWindow(EditorWindow window);   // 닫혀 있으면 다시 열고, 앞으로 꺼낸다
	void CloseWindow(EditorWindow window);
	void OpenAllWindows();

	// 패널의 Begin 바로 앞에서 호출. false면 창이 닫혀 있으므로 Begin/End를 모두 하지 않는다.
	bool PrepareWindow(EditorWindow window);
	// 패널의 Begin 바로 뒤에서 호출. 이름 탭(도킹) 또는 제목 줄(떠 있는 창)을 휠 클릭하면 닫는다.
	void HandleWindowTitleClick(EditorWindow window);

	std::string GetClosedWindowList() const;            // "Game,Inspector"
	void        SetClosedWindowList(const std::string& list);

	// --- 미뤄 둔 씬 변경 ---
	std::string pendingCreateKind;     // "Empty" / "Quad" / "Camera"
	ObjectRef   pendingCreateParent;
	ObjectRef   pendingDelete;
	ObjectRef   pendingReparentChild;
	ObjectRef   pendingReparentParent; // 비어 있으면 루트로
	bool        hasPendingReparent = false;
	std::string pendingLoad;           // 열 씬 경로
	bool        pendingNew = false;

	// 모든 패널을 그린 뒤 호출. 씬 교체(열기/새 씬)는 맨 마지막이므로 이후에는 이전 Scene* 를 쓰면 안 된다.
	void ApplyPendingEdits();

private:
	static constexpr int kWindowCount = static_cast<int>(EditorWindow::Count);
	bool windowOpen[kWindowCount]         = { true, true, true, true, true, true };
	bool windowFocusRequest[kWindowCount] = {};

	GameObject* CreateObject(Scene* scene, const char* kind, GameObject* parent);
};
