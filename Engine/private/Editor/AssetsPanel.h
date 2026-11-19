#pragma once

#include "EditorContext.h"

#include <string>
#include <vector>


// Assets 창: Assets 폴더 파일 로드/리로드/언로드, 파일 감시 토글, 머티리얼 생성/수정/삭제
class AssetsPanel
{
public:
	explicit AssetsPanel(EditorContext& context) : _ctx(context) {}

	void Draw();

private:
	EditorContext& _ctx;

	void DrawFiles();
	void ScanFiles();
	void DrawMaterials();

	std::vector<std::string> _files;   // Assets 기준 상대 경로 (UTF-8, '/')
	u64         _lastScan = 0;
	std::string _newMaterialName = "NewMaterial";
};
