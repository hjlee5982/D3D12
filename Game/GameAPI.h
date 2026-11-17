#pragma once

#include "BuildInfo.h"
#include "Types.h"

class Engine;
class ComponentRegistry;

// Game.dll 수명 주기
//
//   최초 실행 : GameRegisterTypes → GameLoad(engine, false)  → 초기 씬/오브젝트를 만든다
//   리로드    : GameUnload(false) → (호스트) Game 컴포넌트 직렬화 후 제거 → DLL 교체
//               → GameRegisterTypes → (호스트) 스냅샷으로 컴포넌트 복원 → GameLoad(engine, true)
//   종료      : GameUnload(true) → (호스트) Game 컴포넌트 제거
//
// 규칙
//  - Game 컴포넌트는 COMPONENT_TYPE을 선언하고 GameRegisterTypes에서 Register<T>()로 등록한다.
//  - 리로드 후에도 유지할 값은 T::Reflect에 등록한다 (등록하지 않은 멤버는 생성자 기본값으로 돌아간다).
//  - Game.dll의 전역 변수는 리로드 때 초기화된다.
extern "C"
{
	using PFN_GameRegisterTypes = void (*)(ComponentRegistry* registry);
	using PFN_GameLoad       = bool (*)(Engine* engine, bool reloaded);
	using PFN_GameUpdate     = void (*)(f32 deltaTime);
	using PFN_GameLateUpdate = void (*)(f32 deltaTime);
	using PFN_GameUnload     = void (*)(bool shutdown);

	// Game.dll이 컴파일된 설정. 호스트가 GameLoad 전에 Engine 값과 비교해 다르면 로드를 거부한다.
	using PFN_GameGetBuildInfo = void (*)(BuildInfo* out);
}

#if defined(GAME_EXPORTS)
#define GAME_API extern "C" __declspec(dllexport)

// C 경계 밖으로 예외가 전파되지 않도록 구현부에서 모든 예외를 처리해야 한다.
GAME_API void GameRegisterTypes(ComponentRegistry* registry);
GAME_API bool GameLoad(Engine* engine, bool reloaded);
GAME_API void GameUpdate(f32 deltaTime);
GAME_API void GameLateUpdate(f32 deltaTime);
GAME_API void GameUnload(bool shutdown);
GAME_API void GameGetBuildInfo(BuildInfo* out);
#endif
