#pragma once

#include "Handle.h"
#include "Types.h"

#include <string>

class Engine;
class GameObject;
class Scene;

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
	GameObject* CreateObject(Scene* scene, const char* kind, GameObject* parent);
};
