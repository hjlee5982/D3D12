#pragma once

#include "GameAPI.h"

#include <string>

class Engine;

// Game.dll 로더 + 핫 리로더.
//
// 원본 Game.dll을 직접 LoadLibrary 하면 파일이 잠겨 실행 중에 다시 빌드할 수 없으므로,
// 매번 HotReload\Game_<pid>_<n>.dll 로 복사(섀도 카피)한 사본을 불러온다.
// 원본의 수정 시각이 바뀌고 빌드가 끝났으면(일정 시간 변화 없음 + 쓰기 잠금 없음)
// 프레임 사이에 새 사본으로 교체한다.
//
// 씬과 오브젝트는 Engine이 소유하므로 리로드 후에도 유지된다.
// Game.dll에서 정의한 컴포넌트는 DLL을 내리기 전에 Engine::ReleaseModuleObjects로 제거되고,
// 새 Game.dll의 GameLoad(engine, true)에서 다시 붙는다.
class GameHost
{
public:
	GameHost() = default;
	~GameHost();

	GameHost(const GameHost&) = delete;
	GameHost& operator=(const GameHost&) = delete;

	// sourceDllPath: 빌드 결과물(원본) 경로. 원본은 잠그지 않는다.
	// 모듈을 불러오고 GameLoad(engine, false)까지 호출한다.
	bool Load(const std::wstring& sourceDllPath, Engine* engine);

	// GameUnload(true) → Game 소유 컴포넌트 제거 → DLL 해제
	void Unload();

	void Update(f32 deltaTime);
	void LateUpdate(f32 deltaTime);

	bool IsLoaded() const;

	// 원본 DLL이 새로 빌드되었으면 교체한다. 교체가 일어났으면 true. 매 프레임 호출.
	bool ReloadIfChanged();

	// 원본 DLL로 즉시 교체한다. 실패하면 기존 모듈을 다시 불러와 유지한다.
	bool Reload();

	u32 GetReloadCount() const;

	// 마지막 실패 원인 (사용자에게 보여줄 메시지)
	const std::wstring& GetLastError() const;

private:
	struct Module
	{
		HMODULE            handle     = nullptr;
		std::wstring       path;      // 섀도 카피 경로
		PFN_GameRegisterTypes registerTypes = nullptr;
		PFN_GameLoad       load       = nullptr;
		PFN_GameUpdate     update     = nullptr;
		PFN_GameLateUpdate lateUpdate = nullptr;
		PFN_GameUnload     unload     = nullptr;
		PFN_GameGetBuildInfo getBuildInfo = nullptr;
		bool               active     = false; // GameLoad 성공 ~ GameUnload 전
	};

	bool LoadModule(Module& out);
	bool Activate(Module& module, bool reloaded);
	void Deactivate(Module& module, bool shutdown);
	static void ReleaseModule(Module& module);
	void CleanupShadowDirectory() const;

private:
	std::wstring _sourcePath;
	std::wstring _shadowDirectory;

	Module  _module;
	Engine* _engine = nullptr;

	FILETIME  _loadedWriteTime{};   // 마지막으로 로드(시도)한 원본의 수정 시각
	FILETIME  _pendingWriteTime{};  // 감지된 새 수정 시각
	ULONGLONG _pendingSince = 0;
	bool      _pending      = false;

	std::wstring _lastError;

	u32 _copyCounter = 0;
	u32 _reloadCount = 0;
};
