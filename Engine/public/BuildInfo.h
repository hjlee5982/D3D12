#pragma once

#include "EngineAPI.h"
#include "Types.h"

#include "Component.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Transform.h"

#include <cstdio>
#include <string>
#include <vector>

// Engine.dll / Game.dll / Client.exe가 서로 호환되게 빌드되었는지 확인하기 위한 정보.
//
// 이 헤더의 MakeBuildInfo()는 inline이라 "포함한 쪽 모듈의 컴파일 설정"으로 계산된다.
// Engine.dll은 자기 값을 EngineGetBuildInfo()로 내보내고, Game.dll/Client.exe는 자기 값과 비교한다.
//
// 잡아내는 경우
//  - Debug/Release 혼용: _ITERATOR_DEBUG_LEVEL이 다르면 std::vector/std::string 크기가 달라
//    GameObject 등의 멤버 위치가 어긋난다 (→ dynamic_cast "no RTTI data", 액세스 위반)
//  - Engine 공개 헤더의 클래스 레이아웃을 바꿨는데 실행 중인 Engine.dll은 이전 빌드인 경우
//
// 공개 클래스의 레이아웃을 바꾸는데 크기가 우연히 같을 수 있는 변경(멤버 순서 변경, 가상 함수 추가 등)을
// 했다면 kEngineAbiVersion을 올린다.
inline constexpr u32 kEngineAbiVersion = 5; // 2: Component 가상 함수 추가(타입 ID, 리로드 훅)
                                            // 3: GameObject ID, 리소스 핸들 (MeshRenderer/ResourceManager)
                                            // 4: 에디터 (컴포넌트 추가/제거 API, Engine 컴포넌트 리플렉션)
                                            // 5: 재생 모드 (Component::Start, 편집 모드 실행 플래그)

struct BuildInfo
{
	u32 abiVersion;
	u32 debug;              // _DEBUG 정의 여부
	u32 iteratorDebugLevel; // _ITERATOR_DEBUG_LEVEL
	u32 sizeofString;
	u32 sizeofVector;
	u32 sizeofComponent;
	u32 sizeofTransform;
	u32 sizeofGameObject;
	u32 sizeofScene;
	u32 sizeofSceneManager;

	bool operator==(const BuildInfo&) const = default;
};

inline constexpr BuildInfo MakeBuildInfo()
{
	return BuildInfo
	{
		kEngineAbiVersion,
#if defined(_DEBUG)
		1u,
#else
		0u,
#endif
		static_cast<u32>(_ITERATOR_DEBUG_LEVEL),
		static_cast<u32>(sizeof(std::string)),
		static_cast<u32>(sizeof(std::vector<int>)),
		static_cast<u32>(sizeof(Component)),
		static_cast<u32>(sizeof(Transform)),
		static_cast<u32>(sizeof(GameObject)),
		static_cast<u32>(sizeof(Scene)),
		static_cast<u32>(sizeof(SceneManager)),
	};
}

// Engine.dll이 컴파일될 때의 값
ENGINE_API BuildInfo EngineGetBuildInfo();

// 사람이 읽을 수 있는 한 줄 요약 (로그/오류 메시지용).
// 호환되지 않는 모듈 사이에서는 std::string을 주고받으면 안 되므로 inline으로 호출한 쪽에서 만든다.
inline std::string BuildInfoToString(const BuildInfo& info)
{
	char buffer[256];
	snprintf(buffer, sizeof(buffer),
		"abi=%u %s idl=%u string=%u vector=%u Component=%u Transform=%u GameObject=%u Scene=%u SceneManager=%u",
		info.abiVersion, info.debug ? "Debug" : "Release", info.iteratorDebugLevel,
		info.sizeofString, info.sizeofVector, info.sizeofComponent, info.sizeofTransform,
		info.sizeofGameObject, info.sizeofScene, info.sizeofSceneManager);
	return buffer;
}
