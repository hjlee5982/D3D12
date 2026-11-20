#pragma once

#include "EditorContext.h"

#include <string>
#include <vector>

class Scene;

// 메인 메뉴 바: File(씬 새로/열기/저장) · Create · View · Window(창 다시 열기) 메뉴, 단축키(Ctrl+S, Ctrl+P, Ctrl+Shift+P)
// 그 아래 도구 줄: 가운데 정렬된 Play / Pause / Step 버튼
class MainMenuBar
{
public:
	explicit MainMenuBar(EditorContext& context) : _ctx(context) {}

	void Draw(Scene* scene);

private:
	EditorContext& _ctx;

	void DrawFileMenu();
	void DrawSaveAsPopup();
	void DrawToolbar();
	void DrawPlayControls();
	void SaveCurrentScene();
	std::vector<std::string> FindSceneFiles() const;

	std::string _saveAsPath = "Scene/Main.scene";
	bool        _openSaveAs = false;
};
