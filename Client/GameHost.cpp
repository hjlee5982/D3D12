#include "pch.h"
#include "GameHost.h"
#include "Engine.h"
#include "BuildInfo.h"
#include "Log.h"
#include "ComponentRegistry.h"

#include <filesystem>

namespace
{
	// 원본 수정 시각이 이 시간 동안 변하지 않아야 빌드가 끝난 것으로 본다.
	constexpr ULONGLONG kDebounceMs = 300;

	std::string ToUtf8(const std::wstring& text)
	{
		if (text.empty())
		{
			return {};
		}
		const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
		std::string result(static_cast<size_t>(size), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
		return result;
	}

	std::wstring ToWide(const std::string& text)
	{
		if (text.empty())
		{
			return {};
		}
		const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
		std::wstring result(static_cast<size_t>(size), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), size);
		return result;
	}

	// Engine.log + 디버거 출력
	void Log(const std::wstring& message)
	{
		EngineLog("[GameHost] %s", ToUtf8(message).c_str());
	}

	bool GetWriteTime(const std::wstring& path, FILETIME& out)
	{
		WIN32_FILE_ATTRIBUTE_DATA data{};
		if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
		{
			return false;
		}
		out = data.ftLastWriteTime;
		return true;
	}

	// 링커가 아직 쓰고 있으면(쓰기 핸들이 열려 있으면) 실패한다.
	bool IsWriteFinished(const std::wstring& path)
	{
		HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			return false;
		}
		::CloseHandle(file);
		return true;
	}
}

GameHost::~GameHost()
{
	Unload();
}

bool GameHost::Load(const std::wstring& sourceDllPath, Engine* engine)
{
	Unload();

	_engine = engine;
	_sourcePath = std::filesystem::absolute(sourceDllPath).wstring();
	_shadowDirectory = (std::filesystem::path(_sourcePath).parent_path() / L"HotReload").wstring();

	std::error_code ec;
	std::filesystem::create_directories(_shadowDirectory, ec);
	CleanupShadowDirectory();

	if (_engine == nullptr || !GetWriteTime(_sourcePath, _loadedWriteTime))
	{
		_lastError = L"Game.dll을 찾을 수 없습니다: " + _sourcePath;
		Log(L"Game DLL not found: " + _sourcePath);
		return false;
	}

	if (!LoadModule(_module))
	{
		return false;
	}

	if (!Activate(_module, false))
	{
		_lastError = L"GameLoad가 실패했습니다. Engine.log를 확인하세요.";
		Deactivate(_module, true);
		ReleaseModule(_module);
		return false;
	}

	return true;
}

void GameHost::Unload()
{
	Deactivate(_module, true);
	ReleaseModule(_module);
	_pending = false;
}

void GameHost::Update(f32 deltaTime)
{
	if (_module.active)
	{
		_module.update(deltaTime);
	}
}

void GameHost::LateUpdate(f32 deltaTime)
{
	if (_module.active)
	{
		_module.lateUpdate(deltaTime);
	}
}

bool GameHost::IsLoaded() const
{
	return _module.handle != nullptr;
}

bool GameHost::ReloadIfChanged()
{
	if (_sourcePath.empty())
	{
		return false;
	}

	FILETIME writeTime{};
	if (!GetWriteTime(_sourcePath, writeTime))
	{
		// 링크 중에 잠시 파일이 없을 수 있다
		return false;
	}

	if (::CompareFileTime(&writeTime, &_loadedWriteTime) == 0)
	{
		_pending = false;
		return false;
	}

	// 새 수정 시각 감지 → 디바운스 시작 (빌드 중이면 시각이 계속 바뀐다)
	if (!_pending || ::CompareFileTime(&writeTime, &_pendingWriteTime) != 0)
	{
		_pending          = true;
		_pendingWriteTime = writeTime;
		_pendingSince     = ::GetTickCount64();
		return false;
	}

	if (::GetTickCount64() - _pendingSince < kDebounceMs || !IsWriteFinished(_sourcePath))
	{
		return false;
	}

	return Reload();
}

bool GameHost::Reload()
{
	FILETIME writeTime{};
	if (!GetWriteTime(_sourcePath, writeTime))
	{
		Log(L"Reload skipped: source DLL not found.");
		return false;
	}

	// 같은 빌드로 실패를 반복하지 않도록, 결과와 상관없이 이 시각은 처리한 것으로 기록
	_loadedWriteTime = writeTime;
	_pending = false;

	// 1) 새 사본을 먼저 로드·검증. 실패하면 기존 모듈은 그대로 동작한다.
	Module next;
	if (!LoadModule(next))
	{
		Log(L"Reload failed: could not load new DLL. Keeping current module. " + _lastError);
		return false;
	}

	// 2) 기존 모듈 비활성화: GameUnload → 이전 Game.dll 코드로 만든 컴포넌트 제거.
	//    씬/오브젝트/Transform은 Engine 소유라서 그대로 남는다.
	Deactivate(_module, false);

	// 3) 새 모듈 활성화 (씬이 이미 있으므로 reloaded = true)
	if (!Activate(next, true))
	{
		Log(L"Reload failed: GameLoad returned false. Restoring previous module.");
		Deactivate(next, false);
		ReleaseModule(next);

		Activate(_module, true);
		return false;
	}

	// 4) 이전 DLL 해제 (이전 모듈의 컴포넌트는 2)에서 이미 제거됨)
	ReleaseModule(_module);
	_module = next;

	++_reloadCount;
	Log(L"Reloaded #" + std::to_wstring(_reloadCount) + L": " + _module.path);
	return true;
}

const std::wstring& GameHost::GetLastError() const
{
	return _lastError;
}

u32 GameHost::GetReloadCount() const
{
	return _reloadCount;
}

bool GameHost::LoadModule(Module& out)
{
	out = Module{};

	const std::wstring shadowPath = _shadowDirectory + L"\\Game_" + std::to_wstring(::GetCurrentProcessId()) + L"_" + std::to_wstring(_copyCounter++) + L".dll";

	if (!::CopyFileW(_sourcePath.c_str(), shadowPath.c_str(), FALSE))
	{
		Log(L"CopyFile failed (error " + std::to_wstring(::GetLastError()) + L"): " + _sourcePath);
		return false;
	}

	out.path   = shadowPath;
	out.handle = ::LoadLibraryW(shadowPath.c_str());
	if (out.handle == nullptr)
	{
		Log(L"LoadLibrary failed (error " + std::to_wstring(::GetLastError()) + L"): " + shadowPath);
		ReleaseModule(out);
		return false;
	}

	out.registerTypes = reinterpret_cast<PFN_GameRegisterTypes>(::GetProcAddress(out.handle, "GameRegisterTypes"));
	out.load       = reinterpret_cast<PFN_GameLoad>      (::GetProcAddress(out.handle, "GameLoad"));
	out.update     = reinterpret_cast<PFN_GameUpdate>    (::GetProcAddress(out.handle, "GameUpdate"));
	out.lateUpdate = reinterpret_cast<PFN_GameLateUpdate>(::GetProcAddress(out.handle, "GameLateUpdate"));
	out.unload       = reinterpret_cast<PFN_GameUnload>      (::GetProcAddress(out.handle, "GameUnload"));
	out.getBuildInfo = reinterpret_cast<PFN_GameGetBuildInfo>(::GetProcAddress(out.handle, "GameGetBuildInfo"));

	if (out.registerTypes == nullptr || out.load == nullptr || out.update == nullptr || out.lateUpdate == nullptr || out.unload == nullptr || out.getBuildInfo == nullptr)
	{
		_lastError = L"Game.dll에 필요한 함수(GameRegisterTypes/GameLoad/GameUpdate/GameLateUpdate/GameUnload/GameGetBuildInfo)가 없습니다.";
		Log(L"Missing game exports in: " + shadowPath);
		ReleaseModule(out);
		return false;
	}

	// Game 코드를 하나라도 실행하기 전에 Engine과 호환되는 빌드인지 확인한다.
	// (Debug/Release 혼용이나 이전 Engine.dll과의 조합이면 객체 레이아웃이 달라 크래시가 난다)
	BuildInfo gameInfo{};
	out.getBuildInfo(&gameInfo);
	const BuildInfo engineInfo = EngineGetBuildInfo();

	if (!(gameInfo == engineInfo))
	{
		_lastError =
			L"Game.dll과 Engine.dll의 빌드가 호환되지 않습니다.\n"
			L"(Debug/Release가 섞였거나, Engine 헤더를 바꿨는데 Engine.dll이 이전 빌드입니다)\n\n"
			L"Engine: " + ToWide(BuildInfoToString(engineInfo)) + L"\n"
			L"Game  : " + ToWide(BuildInfoToString(gameInfo)) + L"\n\n"
			L"Engine을 바꿨다면 엔진을 종료하고 솔루션 전체를 다시 빌드하세요.";
		Log(L"Build mismatch. " + _lastError);
		ReleaseModule(out);
		return false;
	}

	return true;
}

bool GameHost::Activate(Module& module, bool reloaded)
{
	if (module.handle == nullptr || _engine == nullptr)
	{
		return false;
	}

	// 1) 타입 등록 → 2) 이전 모듈에서 직렬화해 둔 컴포넌트 복원 → 3) GameLoad
	module.registerTypes(&_engine->GetComponentRegistry());
	_engine->RestoreModuleObjects();

	module.active = module.load(_engine, reloaded);
	return module.active;
}

void GameHost::Deactivate(Module& module, bool shutdown)
{
	if (module.handle == nullptr)
	{
		return;
	}

	if (module.active)
	{
		module.unload(shutdown);
		module.active = false;
	}

	// 이 DLL의 컴포넌트를 DLL이 메모리에 있을 때 제거한다 (리로드면 직렬화 후 제거).
	// GameLoad가 실패하면서 일부 컴포넌트를 붙였을 수도 있으므로 active 여부와 무관하게 수행.
	if (_engine != nullptr)
	{
		_engine->ReleaseModuleObjects(module.handle, !shutdown);
	}
}

void GameHost::ReleaseModule(Module& module)
{
	if (module.handle != nullptr)
	{
		::FreeLibrary(module.handle);
	}

	if (!module.path.empty())
	{
		::DeleteFileW(module.path.c_str()); // 아직 잠겨 있으면 실패해도 무시 (다음 실행 때 정리)
	}

	module = Module{};
}

void GameHost::CleanupShadowDirectory() const
{
	// 이전 실행에서 남은 사본 정리. 다른 인스턴스가 사용 중인 파일은 삭제에 실패하고 그대로 남는다.
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(_shadowDirectory, ec))
	{
		if (entry.path().extension() == L".dll")
		{
			::DeleteFileW(entry.path().c_str());
		}
	}
}
