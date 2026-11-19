#include "pch.h"
#include "TransformGizmo.h"
#include "EditorUtils.h"

#include "GameObject.h"
#include "Log.h"
#include "Transform.h"

#include "imgui.h"

#include <cmath>
#include <cstdio>

using namespace EditorUtil;

namespace
{
	// 직선 L(t) = point + t * axis 위에서 광선 R(s) = origin + s * direction 과 가장 가까운 점의 t.
	// 두 직선이 거의 평행이면 false.
	bool ClosestParamOnLine(const vec3& point, const vec3& axis, const vec3& origin, const vec3& direction, f32& t)
	{
		const vec3 w0 = point - origin;
		const f32 a = axis.Dot(axis);
		const f32 b = axis.Dot(direction);
		const f32 c = direction.Dot(direction);
		const f32 d = axis.Dot(w0);
		const f32 e = direction.Dot(w0);
		const f32 denominator = a * c - b * b;
		if (std::fabs(denominator) < 1e-9f * a * c)
		{
			return false;
		}
		t = (b * e - c * d) / denominator;
		return true;
	}

	// 광선과 평면(점 point, 법선 normal)의 교점. 평행하거나 카메라 뒤면 false.
	bool IntersectRayPlane(const vec3& origin, const vec3& direction, const vec3& point, const vec3& normal, vec3& hit)
	{
		const f32 denominator = direction.Dot(normal);
		if (std::fabs(denominator) < 1e-6f * direction.Length())
		{
			return false;
		}
		const f32 s = (point - origin).Dot(normal) / denominator;
		if (s < 0.0f)
		{
			return false;
		}
		hit = origin + direction * s;
		return true;
	}

	f32 DistanceToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b)
	{
		const f32 abx = b.x - a.x, aby = b.y - a.y;
		const f32 lengthSquared = abx * abx + aby * aby;
		f32 t = lengthSquared > 0.0f ? ((p.x - a.x) * abx + (p.y - a.y) * aby) / lengthSquared : 0.0f;
		t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
		const f32 dx = p.x - (a.x + abx * t), dy = p.y - (a.y + aby * t);
		return std::sqrt(dx * dx + dy * dy);
	}

	bool PointInConvexQuad(const ImVec2& p, const ImVec2 quad[4])
	{
		int sign = 0;
		for (int i = 0; i < 4; ++i)
		{
			const ImVec2& a = quad[i];
			const ImVec2& b = quad[(i + 1) % 4];
			const f32 cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
			const int s = cross > 0.0f ? 1 : (cross < 0.0f ? -1 : 0);
			if (s != 0)
			{
				if (sign != 0 && s != sign)
				{
					return false;
				}
				sign = s;
			}
		}
		return true;
	}

	f32 Snap(f32 value, f32 step)
	{
		return std::round(value / step) * step;
	}

	// 월드 값 → 부모 기준 로컬 값으로 바꿔 적용 (자식 오브젝트도 화면에서 본 대로 움직이도록)
	void SetWorldPosition(Transform* transform, const vec3& worldPosition)
	{
		const Transform* parent = transform->GetParent();
		transform->SetPosition(parent != nullptr ? TransformPoint(worldPosition, parent->GetWorldMatrix().Invert()) : worldPosition);
	}

	void SetWorldRotation(Transform* transform, const quat& worldRotation)
	{
		const Transform* parent = transform->GetParent();
		if (parent == nullptr)
		{
			transform->SetRotation(worldRotation);
			return;
		}

		vec3 parentScale;
		quat parentRotation;
		vec3 parentPosition;
		matx parentWorld = parent->GetWorldMatrix();
		if (!parentWorld.Decompose(parentScale, parentRotation, parentPosition))
		{
			return;
		}

		// world = local * parent  →  local = world * parent⁻¹
		quat inverseParent;
		parentRotation.Inverse(inverseParent);
		quat local = worldRotation * inverseParent;
		local.Normalize();
		transform->SetRotation(local);
	}
}

bool TransformGizmo::Update(EditorContext& ctx, const SceneViewport& viewport, bool hovered, bool navigating)
{
	GameObject* selected = ctx.selected.Get();
	const f32 width  = viewport.rect[2] - viewport.rect[0];
	const f32 height = viewport.rect[3] - viewport.rect[1];
	if (selected == nullptr || selected->transform == nullptr || width <= 0.0f || height <= 0.0f)
	{
		_active = -1;
		_hover  = -1;
		return false;
	}

	Transform* transform = selected->transform;
	const ImGuiIO& io = ImGui::GetIO();

	matx view;
	matx projection;
	view       = viewport.view;
	projection = viewport.projection;
	const matx viewProjection = view * projection;

	// 현재 월드 위치/회전
	vec3 worldScale;
	quat worldRotation = quat::Identity;
	vec3 worldPosition = transform->GetWorldPosition();
	{
		matx world = transform->GetWorldMatrix();
		vec3 position;
		if (world.Decompose(worldScale, worldRotation, position))
		{
			worldPosition = position;
		}
	}

	// 축: 드래그 중에는 시작 시점 값으로 고정 (로컬 축이 회전하면서 흔들리지 않도록)
	vec3 axes[3];
	const vec3 unit[3] = { vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f) };
	for (int i = 0; i < 3; ++i)
	{
		axes[i] = _active >= 0 ? _axes[i] : (_local ? RotateVector(unit[i], worldRotation) : unit[i]);
	}

	// 화면에서 항상 같은 크기: 기즈모 중심 깊이에서 화면 1픽셀의 월드 길이
	const vec3 centerInView = TransformPoint(worldPosition, view);
	if (centerInView.z <= 0.01f)
	{
		return false; // 카메라 뒤
	}
	const f32 worldPerPixel = 2.0f * centerInView.z / (projection._22 * height);
	const f32 size = 90.0f * worldPerPixel;

	auto toScreen = [&](const vec3& p, ImVec2& out) -> bool
	{
		const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(DirectX::XMVectorSet(p.x, p.y, p.z, 1.0f), viewProjection);
		const f32 w = DirectX::XMVectorGetW(clip);
		if (w <= 1e-4f)
		{
			return false;
		}
		out = ImVec2(viewport.rect[0] + (DirectX::XMVectorGetX(clip) / w * 0.5f + 0.5f) * width,
		             viewport.rect[1] + (0.5f - DirectX::XMVectorGetY(clip) / w * 0.5f) * height);
		return true;
	};

	ImVec2 center;
	if (!toScreen(worldPosition, center))
	{
		return false;
	}

	const ImVec2 mouse = io.MousePos;

	// --- 핸들의 화면 도형 ---
	ImVec2 tips[3];
	bool   axisVisible[3]{};
	ImVec2 planes[3][4];
	bool   planeVisible[3]{};
	constexpr int kRingSegments = 64;
	ImVec2 rings[3][kRingSegments];
	bool   ringVisible[3]{};

	if (_mode == Mode::Translate)
	{
		for (int i = 0; i < 3; ++i)
		{
			// 축이 시선과 거의 평행하면(화면에서 너무 짧으면) 숨긴다: 잡아도 제대로 움직일 수 없음
			axisVisible[i] = toScreen(worldPosition + axes[i] * size, tips[i])
				&& (tips[i].x - center.x) * (tips[i].x - center.x) + (tips[i].y - center.y) * (tips[i].y - center.y) > 12.0f * 12.0f;

			const vec3& a = axes[(i + 1) % 3];
			const vec3& b = axes[(i + 2) % 3];
			const vec3 corners[4] =
			{
				worldPosition + (a * 0.20f + b * 0.20f) * size,
				worldPosition + (a * 0.45f + b * 0.20f) * size,
				worldPosition + (a * 0.45f + b * 0.45f) * size,
				worldPosition + (a * 0.20f + b * 0.45f) * size,
			};
			planeVisible[i] = true;
			for (int k = 0; k < 4; ++k)
			{
				planeVisible[i] = planeVisible[i] && toScreen(corners[k], planes[i][k]);
			}
		}
	}
	else
	{
		for (int i = 0; i < 3; ++i)
		{
			const vec3& u = axes[(i + 1) % 3];
			const vec3& v = axes[(i + 2) % 3];
			ringVisible[i] = true;
			for (int k = 0; k < kRingSegments; ++k)
			{
				const f32 angle = DirectX::XM_2PI * static_cast<f32>(k) / kRingSegments;
				ringVisible[i] = ringVisible[i] && toScreen(worldPosition + (u * std::cos(angle) + v * std::sin(angle)) * (size * 0.85f), rings[i][k]);
			}
		}
	}

	// --- 마우스가 올라간 핸들 ---
	int hover = -1;
	if (hovered && !navigating && _active < 0)
	{
		constexpr f32 kPickDistance = 7.0f;
		f32 best = kPickDistance;

		if (_mode == Mode::Translate)
		{
			for (int i = 0; i < 3; ++i)
			{
				if (axisVisible[i])
				{
					const f32 distance = DistanceToSegment(mouse, center, tips[i]);
					if (distance < best)
					{
						best  = distance;
						hover = i;
					}
				}
			}
			for (int i = 0; i < 3 && hover < 0; ++i)
			{
				if (planeVisible[i] && PointInConvexQuad(mouse, planes[i]))
				{
					hover = 3 + i;
				}
			}
		}
		else
		{
			for (int i = 0; i < 3; ++i)
			{
				if (!ringVisible[i])
				{
					continue;
				}
				for (int k = 0; k < kRingSegments; ++k)
				{
					const f32 distance = DistanceToSegment(mouse, rings[i][k], rings[i][(k + 1) % kRingSegments]);
					if (distance < best)
					{
						best  = distance;
						hover = i;
					}
				}
			}
		}
	}
	_hover = hover;

	// --- 드래그 시작 ---
	vec3 rayOrigin;
	vec3 rayDirection;
	const bool hasRay = viewport.MouseRay(mouse.x, mouse.y, rayOrigin, rayDirection);

	if (_hover >= 0 && hasRay && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		bool started = false;
		const int handle = _hover;

		if (_mode == Mode::Translate && handle < 3)
		{
			started = ClosestParamOnLine(worldPosition, axes[handle], rayOrigin, rayDirection, _startParam);
		}
		else
		{
			// 이동 평면(법선 = 그 축) 또는 회전 링의 평면(법선 = 회전축)
			const vec3& normal = axes[handle % 3];
			started = IntersectRayPlane(rayOrigin, rayDirection, worldPosition, normal, _startHit);
		}

		if (started)
		{
			_active        = handle;
			_startPosition = worldPosition;
			_startRotation = worldRotation;
			_dragValue     = 0.0f;
			for (int i = 0; i < 3; ++i)
			{
				_axes[i] = axes[i];
			}
		}
		_consumedClick = true; // 시작에 실패해도(평행 등) 선택 클릭으로 처리하지 않는다
	}

	// --- 드래그 중 ---
	if (_active >= 0)
	{
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			EngineLog("[Editor] %s '%s' (%s %.3f)", _mode == Mode::Translate ? "Moved" : "Rotated",
				selected->GetName().c_str(), _mode == Mode::Translate ? "distance" : "degrees", _dragValue);
			_active = -1;
		}
		else if (hasRay)
		{
			const bool snap = io.KeyCtrl;
			constexpr f32 kMoveSnap   = 0.25f;
			constexpr f32 kRotateSnap = DirectX::XMConvertToRadians(15.0f);

			if (_mode == Mode::Translate && _active < 3)
			{
				// 축 이동: 마우스 광선과 축의 가장 가까운 점까지
				const vec3& axis = _axes[_active];
				f32 t = 0.0f;
				if (ClosestParamOnLine(_startPosition, axis, rayOrigin, rayDirection, t))
				{
					f32 distance = t - _startParam;
					distance = snap ? Snap(distance, kMoveSnap) : distance;
					SetWorldPosition(transform, _startPosition + axis * distance);
					_dragValue = distance;
				}
			}
			else if (_mode == Mode::Translate)
			{
				// 평면 이동: 마우스 광선과 평면의 교점 이동량 (평면의 두 축 성분만)
				const int normalIndex = _active - 3;
				const vec3& a = _axes[(normalIndex + 1) % 3];
				const vec3& b = _axes[(normalIndex + 2) % 3];
				vec3 hit;
				if (IntersectRayPlane(rayOrigin, rayDirection, _startPosition, _axes[normalIndex], hit))
				{
					const vec3 delta = hit - _startHit;
					f32 da = delta.Dot(a);
					f32 db = delta.Dot(b);
					if (snap)
					{
						da = Snap(da, kMoveSnap);
						db = Snap(db, kMoveSnap);
					}
					SetWorldPosition(transform, _startPosition + a * da + b * db);
					_dragValue = std::sqrt(da * da + db * db);
				}
			}
			else
			{
				// 회전: 시작 교점 → 현재 교점이 회전축 둘레로 이루는 부호 있는 각도
				const vec3& axis = _axes[_active];
				vec3 hit;
				if (IntersectRayPlane(rayOrigin, rayDirection, _startPosition, axis, hit))
				{
					vec3 from = _startHit - _startPosition;
					vec3 to   = hit - _startPosition;
					if (from.LengthSquared() > 1e-10f && to.LengthSquared() > 1e-10f)
					{
						from.Normalize();
						to.Normalize();
						f32 angle = std::atan2(from.Cross(to).Dot(axis), from.Dot(to));
						angle = snap ? Snap(angle, kRotateSnap) : angle;

						// 월드 축 기준 회전을 시작 회전 뒤에 적용
						quat rotation = _startRotation * quat::CreateFromAxisAngle(axis, angle);
						rotation.Normalize();
						SetWorldRotation(transform, rotation);
						_dragValue = DirectX::XMConvertToDegrees(angle);
					}
				}
			}
		}
	}

	// --- 그리기 ---
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(ImVec2(viewport.rect[0], viewport.rect[1]), ImVec2(viewport.rect[2], viewport.rect[3]), true);

	const ImU32 axisColors[3] = { IM_COL32(230, 60, 60, 255), IM_COL32(80, 210, 80, 255), IM_COL32(70, 120, 255, 255) };
	const ImU32 highlight = IM_COL32(255, 220, 60, 255);
	auto colorOf = [&](int handle, int axis) -> ImU32
	{
		return (_active == handle || (_active < 0 && _hover == handle)) ? highlight : axisColors[axis];
	};

	if (_mode == Mode::Translate)
	{
		for (int i = 0; i < 3; ++i)
		{
			if (!planeVisible[i])
			{
				continue;
			}
			const ImU32 color = colorOf(3 + i, i);
			drawList->AddConvexPolyFilled(planes[i], 4, (color & 0x00FFFFFF) | 0x50000000);
			drawList->AddPolyline(planes[i], 4, color, ImDrawFlags_Closed, 1.5f);
		}

		for (int i = 0; i < 3; ++i)
		{
			if (!axisVisible[i])
			{
				continue;
			}
			const ImU32 color = colorOf(i, i);
			drawList->AddLine(center, tips[i], color, 3.0f);

			// 화살촉
			f32 dx = tips[i].x - center.x;
			f32 dy = tips[i].y - center.y;
			const f32 length = std::sqrt(dx * dx + dy * dy);
			dx /= length;
			dy /= length;
			drawList->AddTriangleFilled(
				ImVec2(tips[i].x + dx * 12.0f, tips[i].y + dy * 12.0f),
				ImVec2(tips[i].x - dy * 6.0f, tips[i].y + dx * 6.0f),
				ImVec2(tips[i].x + dy * 6.0f, tips[i].y - dx * 6.0f),
				color);
		}
	}
	else
	{
		for (int i = 0; i < 3; ++i)
		{
			if (ringVisible[i])
			{
				const bool emphasized = _active == i || (_active < 0 && _hover == i);
				drawList->AddPolyline(rings[i], kRingSegments, colorOf(i, i), ImDrawFlags_Closed, emphasized ? 3.5f : 2.5f);
			}
		}

		// 회전 중: 중심 → 시작점, 중심 → 현재 마우스
		if (_active >= 0)
		{
			ImVec2 start;
			if (toScreen(_startHit, start))
			{
				drawList->AddLine(center, start, IM_COL32(255, 255, 255, 120), 1.0f);
			}
			drawList->AddLine(center, mouse, IM_COL32(255, 255, 255, 200), 1.0f);
		}
	}

	drawList->AddCircleFilled(center, 4.0f, IM_COL32(255, 255, 255, 230));

	if (_active >= 0)
	{
		char label[64];
		if (_mode == Mode::Translate)
		{
			std::snprintf(label, sizeof(label), "%+.3f%s", _dragValue, io.KeyCtrl ? "  (snap)" : "");
		}
		else
		{
			std::snprintf(label, sizeof(label), "%+.1f deg%s", _dragValue, io.KeyCtrl ? "  (snap)" : "");
		}
		drawList->AddText(ImVec2(center.x + 14.0f, center.y + 10.0f), IM_COL32(255, 255, 255, 255), label);
	}

	drawList->PopClipRect();
	return _consumedClick;
}
