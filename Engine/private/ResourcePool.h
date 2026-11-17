#pragma once

#include "Handle.h"

#include <memory>
#include <vector>

// 세대 기반 슬롯 풀 (Engine 내부 전용).
//  - Add: 빈 슬롯을 재사용하고 그 슬롯의 현재 세대를 담은 핸들을 돌려준다.
//  - Remove: 객체를 해제하고 세대를 올린다 → 이전 핸들은 Get에서 nullptr.
//  - 슬롯 객체는 unique_ptr이라 풀이 커져도 객체 주소는 바뀌지 않는다.
template <typename T>
class ResourcePool
{
public:
	Handle<T> Add(std::unique_ptr<T> object)
	{
		u32 index = 0;
		if (!_free.empty())
		{
			index = _free.back();
			_free.pop_back();
		}
		else
		{
			index = static_cast<u32>(_slots.size());
			_slots.emplace_back();
		}

		Slot& slot = _slots[index];
		slot.object = std::move(object);
		++_count;
		return Handle<T>{ index, slot.generation };
	}

	T* Get(Handle<T> handle) const
	{
		if (!handle.IsValid() || handle.index >= _slots.size())
		{
			return nullptr;
		}

		const Slot& slot = _slots[handle.index];
		return slot.generation == handle.generation ? slot.object.get() : nullptr;
	}

	bool Remove(Handle<T> handle)
	{
		if (Get(handle) == nullptr)
		{
			return false;
		}

		Slot& slot = _slots[handle.index];
		slot.object.reset();
		slot.generation = slot.generation == UINT32_MAX ? 1u : slot.generation + 1u; // 0은 "없음"이므로 건너뜀
		_free.push_back(handle.index);
		--_count;
		return true;
	}

	template <typename Fn>
	void ForEach(Fn&& fn)
	{
		for (u32 i = 0; i < _slots.size(); ++i)
		{
			if (_slots[i].object)
			{
				fn(Handle<T>{ i, _slots[i].generation }, *_slots[i].object);
			}
		}
	}

	template <typename Fn>
	void ForEach(Fn&& fn) const
	{
		for (u32 i = 0; i < _slots.size(); ++i)
		{
			if (_slots[i].object)
			{
				fn(Handle<T>{ i, _slots[i].generation }, static_cast<const T&>(*_slots[i].object));
			}
		}
	}

	void Clear()
	{
		for (u32 i = 0; i < _slots.size(); ++i)
		{
			if (_slots[i].object)
			{
				Remove(Handle<T>{ i, _slots[i].generation });
			}
		}
	}

	u32 GetCount() const { return _count; }

private:
	struct Slot
	{
		std::unique_ptr<T> object;
		u32 generation = 1;
	};

	std::vector<Slot> _slots;
	std::vector<u32>  _free;
	u32 _count = 0;
};
