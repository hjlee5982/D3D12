#pragma once

// 씬 파일용 최소 JSON (Engine 내부 전용).
//  - 객체 키 순서를 유지한다 (저장 파일 diff가 안정적)
//  - 숫자는 double. 쓰기는 %.9g라 f32는 손실 없이 왕복한다.
//  - 문자열은 UTF-8. \uXXXX(서로게이트 포함)를 읽을 수 있다.

#include <string>
#include <utility>
#include <vector>

class Json
{
public:
	enum class Type { Null, Bool, Number, String, Array, Object };

	Json() = default;
	Json(std::nullptr_t) {}
	Json(bool value) : _type(Type::Bool), _bool(value) {}
	Json(double value) : _type(Type::Number), _number(value) {}
	Json(float value) : Json(static_cast<double>(value)) {}
	Json(int value) : Json(static_cast<double>(value)) {}
	Json(unsigned value) : Json(static_cast<double>(value)) {}
	Json(unsigned long long value) : Json(static_cast<double>(value)) {}
	Json(const char* value) : _type(Type::String), _string(value) {}
	Json(std::string value) : _type(Type::String), _string(std::move(value)) {}

	static Json MakeArray()  { Json j; j._type = Type::Array;  return j; }
	static Json MakeObject() { Json j; j._type = Type::Object; return j; }

	Type GetType() const { return _type; }
	bool IsNull() const   { return _type == Type::Null; }
	bool IsBool() const   { return _type == Type::Bool; }
	bool IsNumber() const { return _type == Type::Number; }
	bool IsString() const { return _type == Type::String; }
	bool IsArray() const  { return _type == Type::Array; }
	bool IsObject() const { return _type == Type::Object; }

	bool               AsBool(bool fallback = false) const;
	double             AsNumber(double fallback = 0.0) const;
	const std::string& AsString() const; // 문자열이 아니면 빈 문자열

	// 배열
	size_t      Size() const;
	const Json& operator[](size_t index) const;
	Json&       Push(Json value);

	// 객체 (없는 키는 Null 반환)
	const Json& operator[](const std::string& key) const;
	Json&       Set(const std::string& key, Json value);
	const std::vector<std::pair<std::string, Json>>& Members() const { return _object; }

	std::string Dump() const;

	// 실패하면 false, error에 "line:col message"
	static bool Parse(const std::string& text, Json& out, std::string& error);

private:
	void DumpTo(std::string& out, int indent) const;

	Type        _type   = Type::Null;
	bool        _bool   = false;
	double      _number = 0.0;
	std::string _string;
	std::vector<Json> _array;
	std::vector<std::pair<std::string, Json>> _object;
};
