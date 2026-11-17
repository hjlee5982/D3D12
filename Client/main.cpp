#include "pch.h"
#include "Engine.h"
#include "BuildInfo.h"
#include "GameHost.h"

namespace
{
	constexpr WCHAR kWindowClassName[] = L"D3D12ClientWindow";
	constexpr WCHAR kWindowTitle[]     = L"D3D12";
	constexpr i32   kDefaultWidth      = 1280;
	constexpr i32   kDefaultHeight     = 720;

	std::unique_ptr<Engine> kEngine = nullptr;
	GameHost kGame;
	bool     kForceReload = false; // F5: 즉시 Game.dll 다시 불러오기

	LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		// 에디터(ImGui)가 먼저 처리한다. 텍스트 입력 중의 Esc/F5 등은 여기서 걸러진다. F1 = 에디터 표시 전환.
		if (kEngine != nullptr && kEngine->EngineHandleWindowMessage(hWnd, msg, wParam, lParam))
		{
			return 0;
		}

		switch (msg)
		{
		case WM_KEYDOWN:

			switch (wParam)
			{
			case VK_ESCAPE:
				DestroyWindow(hWnd);
				break;

			case VK_F5:
				kForceReload = true;
				break;
			}
			return 0;

		case WM_SIZE:
			// 최소화 시(0x0)는 Device 쪽에서 무시한다. 엔진 생성 전 WM_SIZE도 안전하게 건너뛴다.
			if (kEngine != nullptr && wParam != SIZE_MINIMIZED)
			{
				kEngine->OnResize(LOWORD(lParam), HIWORD(lParam));
			}
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		default:
			return DefWindowProcW(hWnd, msg, wParam, lParam);
		}
	}

	// 작업 디렉터리와 무관하게 Client.exe 옆의 Game.dll을 사용한다.
	std::wstring GetGameDllPath()
	{
		WCHAR exePath[MAX_PATH]{};
		::GetModuleFileNameW(nullptr, exePath, MAX_PATH);

		std::wstring path(exePath);
		const size_t slash = path.find_last_of(L"\\/");
		path.resize(slash == std::wstring::npos ? 0 : slash + 1);
		return path + L"Game.dll";
	}

	// UTF-8 메시지를 오류 창으로 표시
	void ShowError(const std::string& utf8Message)
	{
		const int size = ::MultiByteToWideChar(CP_UTF8, 0, utf8Message.c_str(), -1, nullptr, 0);
		std::wstring message(static_cast<size_t>(size > 0 ? size : 1), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, utf8Message.c_str(), -1, message.data(), size);
		::MessageBoxW(nullptr, message.c_str(), L"D3D12", MB_OK | MB_ICONERROR);
	}

	void UpdateWindowTitle(HWND hWnd)
	{
		const std::wstring title = std::wstring(kWindowTitle) + L"  [Game reload #" + std::to_wstring(kGame.GetReloadCount()) + L"]";
		::SetWindowTextW(hWnd, title.c_str());
	}

	HWND CreateClientWindow(HINSTANCE instance, i32 showCommand)
	{
		WNDCLASSEXW windowClass{};
		{
			windowClass.cbSize        = sizeof(windowClass);
			windowClass.style         = CS_HREDRAW | CS_VREDRAW;
			windowClass.lpfnWndProc   = WndProc;
			windowClass.hInstance     = instance;
			windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
			windowClass.hbrBackground = nullptr; // D3D가 전체를 그리므로 GDI 배경 지우기 생략 (리사이즈 깜빡임 방지)
			windowClass.lpszClassName = kWindowClassName;
		}
		if (RegisterClassExW(&windowClass) == 0)
		{
			return nullptr;
		}

		RECT windowRect{ 0, 0, kDefaultWidth, kDefaultHeight };
		AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

		const i32 windowWidth  = windowRect.right  - windowRect.left;
		const i32 windowHeight = windowRect.bottom - windowRect.top;

		HWND hWnd = CreateWindowExW(
			0,
			kWindowClassName,
			kWindowTitle,
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			windowWidth,
			windowHeight,
			nullptr,
			nullptr,
			instance,
			nullptr);

		if (hWnd == nullptr)
		{
			return nullptr;
		}

		ShowWindow(hWnd, showCommand);
		UpdateWindow(hWnd);
		return hWnd;
	}
}

i32 WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ i32 showCommand)
{
	// Client.exe와 Engine.dll이 같은 설정(Debug/Release)으로 빌드되었는지 먼저 확인.
	// 다르면 Engine 객체를 만드는 순간 메모리 레이아웃이 어긋나 크래시가 날 수 있다.
	{
		const BuildInfo clientInfo = MakeBuildInfo();
		const BuildInfo engineInfo = EngineGetBuildInfo();
		if (!(clientInfo == engineInfo))
		{
			const std::string detail =
				"Client.exe와 Engine.dll의 빌드가 호환되지 않습니다 (Debug/Release 혼용 등).\n\n"
				"Engine: " + BuildInfoToString(engineInfo) + "\n"
				"Client: " + BuildInfoToString(clientInfo) + "\n\n"
				"솔루션 전체를 같은 구성으로 다시 빌드하세요.";
			ShowError(detail);
			return 1;
		}
	}

	// Create Window
	HWND hWnd = CreateClientWindow(instance, showCommand);
	if (hWnd == nullptr)
	{
		return 1;
	}

	// Initialize Engine
	{
		kEngine = std::make_unique<Engine>(11);
		if (!kEngine->EngineInitialize(hWnd))
		{
			ShowError("Engine 초기화에 실패했습니다. Engine.log를 확인하세요.");
			DestroyWindow(hWnd);
			return 1;
		}

		kEngine->SetTargetFPS(30);
		kEngine->SetVSync(false); // 목표 FPS 제한과 VSync 대기가 겹치지 않도록
	}
	
	// Link Game
	{
		if (!kGame.Load(GetGameDllPath(), kEngine.get()))
		{
			::MessageBoxW(hWnd, kGame.GetLastError().c_str(), L"Game.dll 로드 실패", MB_OK | MB_ICONERROR);
			kEngine->EngineShutdown();
			DestroyWindow(hWnd);
			return 1;
		}
	}
	

	bool running = true;
	while (running)
	{
		MSG message{};
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
		{
			if (message.message == WM_QUIT)
			{
				running = false;
				break;
			}

			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		if (!running)
		{
			break;
		}

		// Hot Reload: 프레임 사이(게임 코드가 실행 중이 아닐 때)에만 Game.dll을 교체한다.
		{
			const bool reloaded = kForceReload ? kGame.Reload() : kGame.ReloadIfChanged();
			kForceReload = false;

			if (reloaded)
			{
				UpdateWindowTitle(hWnd);
			}
		}

		kEngine->EngineUpdate();

		const f32 deltaTime = kEngine->GetDeltaTime();
		kGame.Update(deltaTime);
		kEngine->EngineSceneUpdate();

		kGame.LateUpdate(deltaTime);
		kEngine->EngineSceneLateUpdate();

		kEngine->EngineRenderBegin();
		kEngine->EngineRender();
		kEngine->EngineRenderEnd();

		kEngine->EngineEndFrame();
	}

	kGame.Unload();
	kEngine->EngineShutdown();

	return 0;
}
