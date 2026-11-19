#pragma once

#include "EditorContext.h"

#include <string>
#include <vector>

class Scene;

// 메인 메뉴 바: File(씬 새로/열기/저장) · Create · View 메뉴, 재생 버튼, 단축키(Ctrl+S, Ctrl+P, Ctrl+Shift+P)
class MainMenuBar
{
public:
	explicit MainMenuBar(EditorContext& context) : _ctx(context) {}

	void Draw(Scene* scene);

private:
	EditorContext& _ctx;

	void DrawFileMenu();
	void DrawSaveAsPopup();
	void DrawPlayControls();
	void SaveCurrentScene();
	std::vector<std::string> FindSceneFiles() const;

	std::string _saveAsPath = "Scene/Main.scene";
	bool        _openSaveAs = false;
};
