#pragma once

#include "Handle.h"
#include "RenderTexture11.h"
#include "Types.h"

#include <string>
#include <vector>

class Engine;
class GameObject;
class Component;
class Scene;
struct FieldInfo;

// ImGui 기반 인게임 에디터 (Engine 내부 전용).
//
// Engine.dll 안에 있으므로 Game.dll을 리로드해도 에디터 상태(선택, 창 배치)가 유지된다.
// 선택한 오브젝트는 ObjectRef로 들고 있어 삭제되면 자동으로 선택 해제된다.
//
//  - Hierarchy : 오브젝트 생성/삭제/이름 변경, 드래그로 부모 변경 (월드 위치 유지)
//  - Inspector : 리플렉션 필드 편집, 컴포넌트 추가/제거/활성화
//  - Assets    : Assets 폴더 파일 로드/리로드/언로드, 머티리얼 생성/수정/삭제
//  - File      : 씬 새로 만들기/열기/저장(Ctrl+S)/다른 이름으로 저장
//  - Scene     : 씬을 화면 밖 텍스처로 그려 표시 (창 크기에 맞춰 해상도 변경)
//
// F1로 표시/숨김. ImGui가 마우스/키보드를 쓰는 중이면 그 입력은 게임/호스트로 가지 않는다.
class Editor
{
public:
	explicit Editor(Engine& engine);
	~Editor();

	Editor(const Editor&) = delete;
	Editor& operator=(const Editor&) = delete;

	bool Initialize(HWND hWnd, ID3D11Device* device, ID3D11DeviceContext* context);
	void Shutdown();

	// true면 에디터가 이 메시지를 사용했다 (호출자는 무시해야 함)
	bool HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	// 씬을 그린 뒤, Present 전에 호출
	void Render();

	// Scene 창: 지난 프레임에 잰 창 크기로 화면 밖 텍스처를 준비해 바인딩하고 지운다.
	// Scene 창이 닫혀 있거나 접혀 있으면 false (씬을 그릴 필요 없음).
	bool BeginSceneView(const f32 clearColor[4]);
	u32  GetSceneViewWidth() const  { return _sceneTexture.GetWidth(); }
	u32  GetSceneViewHeight() const { return _sceneTexture.GetHeight(); }

	// Scene 창이 에디터 카메라 모드면 그 행렬을 채우고 true. 게임 카메라 모드면 false (씬의 카메라 사용).
	bool GetSceneViewCamera(matx& view, matx& projection) const;

	void SetVisible(bool visible) { _visible = visible; }
	bool IsVisible() const { return _visible; }

private:
	void DrawMainMenu(Scene* scene);
	void DrawFileMenu();
	void DrawSaveAsPopup();
	void SaveCurrentScene();
	std::vector<std::string> FindSceneFiles() const;
	void DrawHierarchy(Scene* scene);
	void DrawHierarchyNode(GameObject* object);
	void DrawInspector();
	void DrawComponent(GameObject* object, Component* component, Component*& removeRequest);
	void DrawField(Component* component, const FieldInfo& field);
	void DrawAssets();
	void DrawSceneView();
	void DrawSceneToolbar();
	void UpdateSceneCamera(bool hovered);
	void PickObject(f32 mouseX, f32 mouseY);
	void DrawSelectionOutline();

	// 기즈모 (선택한 오브젝트 이동/회전)
	bool UpdateGizmo(bool hovered); // 이번 클릭을 기즈모가 썼으면 true (선택 클릭으로 처리하지 않음)
	bool GetMouseRay(f32 mouseX, f32 mouseY, vec3& origin, vec3& direction) const;
	void DrawObjectDetails(GameObject* object);
	void DrawComponentDetails(Component* component);

	// 지금 Scene 창에 표시 중인 카메라 행렬 (모드에 따라 에디터/게임 카메라)
	void GetDisplayedCamera(matx& view, matx& projection) const;
	vec3 GetEditorCameraForward() const;
	void BuildDefaultLayout(unsigned int dockspaceId);
	void DrawAssetFiles();
	void DrawMaterials();

	GameObject* CreateObject(Scene* scene, const char* kind, GameObject* parent);
	void ScanAssetFiles();

private:
	Engine& _engine;
	bool _initialized = false;
	bool _visible     = true;
	bool _showDemo    = false;
	bool _resetLayout = false;

	ID3D11Device*        _device  = nullptr;
	ID3D11DeviceContext* _context = nullptr;

	// Scene 창
	RenderTexture11 _sceneTexture;
	u32  _sceneViewWidth   = 0; // 이번 프레임에 잰 Scene 창 내용 영역 크기 (다음 프레임 렌더에 사용)
	u32  _sceneViewHeight  = 0;
	bool _sceneViewHovered = false;
	f32  _sceneRect[4]{};       // Scene 이미지의 화면 좌표 (minX, minY, maxX, maxY)

	// 에디터 카메라 (씬과 별개. 씬 파일에 저장되지 않는다)
	bool _useGameCamera = false;
	vec3 _cameraPosition{ 0.0f, 1.0f, -5.0f };
	f32  _cameraYaw     = 0.0f;  // 라디안. +는 오른쪽
	f32  _cameraPitch   = 0.2f;  // 라디안. +는 아래
	f32  _cameraSpeed   = 4.0f;  // 단위/초
	f32  _cameraFovY    = 60.0f; // 도
	bool _navigating    = false; // 오른쪽/가운데 버튼 드래그 중

	// 기즈모
	enum class GizmoMode : u8 { Translate, Rotate };
	GizmoMode _gizmoMode  = GizmoMode::Translate;
	bool      _gizmoLocal = false; // true: 오브젝트 축, false: 월드 축
	int       _gizmoHover  = -1;   // 이동: 0~2 축, 3~5 평면(법선 축 번호 + 3). 회전: 0~2 링
	int       _gizmoActive = -1;   // 드래그 중인 핸들
	bool      _gizmoConsumedClick = false;
	vec3      _gizmoAxes[3];       // 드래그 시작 시점의 축 (드래그 중 고정)
	vec3      _gizmoStartPosition; // 드래그 시작 시 월드 위치
	quat      _gizmoStartRotation; // 드래그 시작 시 월드 회전
	vec3      _gizmoStartHit;      // 시작 시 마우스 광선과 축/평면의 교점
	f32       _gizmoStartParam = 0.0f;
	f32       _gizmoDragValue  = 0.0f; // 표시용 (이동 거리 또는 회전 각도)

	std::string _iniPath;

	ObjectRef _selected;

	// 프레임 도중에 씬을 바꾸지 않도록 모아 두었다가 패널을 다 그린 뒤 처리
	ObjectRef _pendingDelete;
	ObjectRef _pendingReparentChild;
	ObjectRef _pendingReparentParent; // 비어 있으면 루트로
	bool      _hasPendingReparent = false;
	std::string _pendingCreateKind;
	ObjectRef   _pendingCreateParent;

	// 씬 파일
	std::string _saveAsPath = "Scene/Main.scene";
	std::string _pendingLoad;       // 프레임 끝에서 불러올 씬 경로
	bool        _pendingNew = false;
	bool        _openSaveAs = false;

	// Assets
	std::vector<std::string> _assetFiles; // Assets 기준 상대 경로 (UTF-8, '/')
	u64            _lastAssetScan = 0;
	MaterialHandle _selectedMaterial;
	std::string    _newMaterialName = "NewMaterial";
};
