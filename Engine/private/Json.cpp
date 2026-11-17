#include "pch.h"
#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
	const Json kNull;
	const std::string kEmpty;

	void AppendEscaped(std::string& out, const std::string& text)
	{
		out.push_back('"');
		for (const char c : text)
		{
			switch (c)
			{
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			case '\b': out += "\\b";  break;
			case '\f': out += "\\f";  break;
			default:
				if (static_cast<unsigned char>(c) < 0x20)
				{
					char buffer[8];
					std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
					out += buffer;
				}
				else
				{
					out.push_back(c); // UTF-8 바이트는 그대로
				}
				break;
			}
		}
		out.push_back('"');
	}

	void AppendIndent(std::string& out, int indent)
	{
		out.append(static_cast<size_t>(indent) * 2, ' ');
	}

	// 짧은 숫자 배열(벡터 등)은 한 줄로 쓴다
	bool IsInlineArray(const std::vector<Json>& items)
	{
		if (items.size() > 4)
		{
			return false;
		}
		for (const Json& item : items)
		{
			if (!item.IsNumber())
			{
				return false;
			}
		}
		return true;
	}

	class Parser
	{
	public:
		explicit Parser(const std::string& text) : _text(text) {}

		bool Run(Json& out, std::string& error)
		{
			SkipWhitespace();
			if (!ParseValue(out, 0))
			{
				error = _error;
				return false;
			}
			SkipWhitespace();
			if (_pos != _text.size())
			{
				Fail("unexpected trailing characters");
				error = _error;
				return false;
			}
			return true;
		}

	private:
		bool Fail(const char* message)
		{
			if (_error.empty())
			{
				size_t line = 1;
				size_t column = 1;
				for (size_t i = 0; i < _pos && i < _text.size(); ++i)
				{
					if (_text[i] == '\n') { ++line; column = 1; }
					else                  { ++column; }
				}
				_error = std::to_string(line) + ":" + std::to_string(column) + " " + message;
			}
			return false;
		}

		void SkipWhitespace()
		{
			while (_pos < _text.size() && (_text[_pos] == ' ' || _text[_pos] == '\t' || _text[_pos] == '\n' || _text[_pos] == '\r'))
			{
				++_pos;
			}
		}

		bool Consume(const char* literal)
		{
			const size_t length = std::char_traits<char>::length(literal);
			if (_text.compare(_pos, length, literal) == 0)
			{
				_pos += length;
				return true;
			}
			return false;
		}

		bool ParseValue(Json& out, int depth)
		{
			if (depth > 64)
			{
				return Fail("nesting too deep");
			}
			if (_pos >= _text.size())
			{
				return Fail("unexpected end of input");
			}

			const char c = _text[_pos];
			if (c == '{') return ParseObject(out, depth);
			if (c == '[') return ParseArray(out, depth);
			if (c == '"')
			{
				std::string text;
				if (!ParseString(text))
				{
					return false;
				}
				out = Json(std::move(text));
				return true;
			}
			if (Consume("true"))  { out = Json(true);    return true; }
			if (Consume("false")) { out = Json(false);   return true; }
			if (Consume("null"))  { out = Json(nullptr); return true; }
			if (c == '-' || (c >= '0' && c <= '9'))
			{
				return ParseNumber(out);
			}
			return Fail("unexpected character");
		}

		bool ParseNumber(Json& out)
		{
			const char* begin = _text.c_str() + _pos;
			char* end = nullptr;
			const double value = std::strtod(begin, &end);
			if (end == begin)
			{
				return Fail("invalid number");
			}
			_pos += static_cast<size_t>(end - begin);
			out = Json(value);
			return true;
		}

		static void AppendUtf8(std::string& out, unsigned codepoint)
		{
			if (codepoint < 0x80)
			{
				out.push_back(static_cast<char>(codepoint));
			}
			else if (codepoint < 0x800)
			{
				out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
				out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			}
			else if (codepoint < 0x10000)
			{
				out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
				out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			}
			else
			{
				out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
				out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			}
		}

		bool ParseHex4(unsigned& out)
		{
			if (_pos + 4 > _text.size())
			{
				return Fail("invalid \\u escape");
			}
			out = 0;
			for (int i = 0; i < 4; ++i)
			{
				const char h = _text[_pos++];
				out <<= 4;
				if (h >= '0' && h <= '9')      out |= static_cast<unsigned>(h - '0');
				else if (h >= 'a' && h <= 'f') out |= static_cast<unsigned>(h - 'a' + 10);
				else if (h >= 'A' && h <= 'F') out |= static_cast<unsigned>(h - 'A' + 10);
				else return Fail("invalid \\u escape");
			}
			return true;
		}

		bool ParseString(std::string& out)
		{
			++_pos; // "
			while (_pos < _text.size())
			{
				const char c = _text[_pos++];
				if (c == '"')
				{
					return true;
				}
				if (c != '\\')
				{
					out.push_back(c);
					continue;
				}
				if (_pos >= _text.size())
				{
					break;
				}

				const char e = _text[_pos++];
				switch (e)
				{
				case '"':  out.push_back('"');  break;
				case '\\': out.push_back('\\'); break;
				case '/':  out.push_back('/');  break;
				case 'b':  out.push_back('\b'); break;
				case 'f':  out.push_back('\f'); break;
				case 'n':  out.push_back('\n'); break;
				case 'r':  out.push_back('\r'); break;
				case 't':  out.push_back('\t'); break;
				case 'u':
				{
					unsigned codepoint = 0;
					if (!ParseHex4(codepoint))
					{
						return false;
					}
					if (codepoint >= 0xD800 && codepoint <= 0xDBFF && Consume("\\u"))
					{
						unsigned low = 0;
						if (!ParseHex4(low))
						{
							return false;
						}
						codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
					}
					AppendUtf8(out, codepoint);
					break;
				}
				default:
					return Fail("invalid escape");
				}
			}
			return Fail("unterminated string");
		}

		bool ParseArray(Json& out, int depth)
		{
			++_pos; // [
			out = Json::MakeArray();
			SkipWhitespace();
			if (Consume("]"))
			{
				return true;
			}

			while (true)
			{
				Json item;
				SkipWhitespace();
				if (!ParseValue(item, depth + 1))
				{
					return false;
				}
				out.Push(std::move(item));
				SkipWhitespace();
				if (Consume(","))
				{
					continue;
				}
				if (Consume("]"))
				{
					return true;
				}
				return Fail("expected ',' or ']'");
			}
		}

		bool ParseObject(Json& out, int depth)
		{
			++_pos; // {
			out = Json::MakeObject();
			SkipWhitespace();
			if (Consume("}"))
			{
				return true;
			}

			while (true)
			{
				SkipWhitespace();
				if (_pos >= _text.size() || _text[_pos] != '"')
				{
					return Fail("expected key string");
				}
				std::string key;
				if (!ParseString(key))
				{
					return false;
				}
				SkipWhitespace();
				if (!Consume(":"))
				{
					return Fail("expected ':'");
				}
				SkipWhitespace();
				Json value;
				if (!ParseValue(value, depth + 1))
				{
					return false;
				}
				out.Set(key, std::move(value));
				SkipWhitespace();
				if (Consume(","))
				{
					continue;
				}
				if (Consume("}"))
				{
					return true;
				}
				return Fail("expected ',' or '}'");
			}
		}

		const std::string& _text;
		size_t _pos = 0;
		std::string _error;
	};
}

bool Json::AsBool(bool fallback) const
{
	return _type == Type::Bool ? _bool : fallback;
}

double Json::AsNumber(double fallback) const
{
	return _type == Type::Number ? _number : fallback;
}

const std::string& Json::AsString() const
{
	return _type == Type::String ? _string : kEmpty;
}

size_t Json::Size() const
{
	return _type == Type::Array ? _array.size() : (_type == Type::Object ? _object.size() : 0);
}

const Json& Json::operator[](size_t index) const
{
	return _type == Type::Array && index < _array.size() ? _array[index] : kNull;
}

Json& Json::Push(Json value)
{
	if (_type != Type::Array)
	{
		*this = MakeArray();
	}
	_array.push_back(std::move(value));
	return _array.back();
}

const Json& Json::operator[](const std::string& key) const
{
	if (_type == Type::Object)
	{
		for (const auto& [name, value] : _object)
		{
			if (name == key)
			{
				return value;
			}
		}
	}
	return kNull;
}

Json& Json::Set(const std::string& key, Json value)
{
	if (_type != Type::Object)
	{
		*this = MakeObject();
	}
	for (auto& [name, existing] : _object)
	{
		if (name == key)
		{
			existing = std::move(value);
			return existing;
		}
	}
	_object.emplace_back(key, std::move(value));
	return _object.back().second;
}

std::string Json::Dump() const
{
	std::string out;
	DumpTo(out, 0);
	out.push_back('\n');
	return out;
}

void Json::DumpTo(std::string& out, int indent) const
{
	switch (_type)
	{
	case Type::Null:
		out += "null";
		break;

	case Type::Bool:
		out += _bool ? "true" : "false";
		break;

	case Type::Number:
	{
		if (!std::isfinite(_number))
		{
			out += "0"; // JSON에는 NaN/Inf가 없다
			break;
		}
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%.9g", _number);
		out += buffer;
		break;
	}

	case Type::String:
		AppendEscaped(out, _string);
		break;

	case Type::Array:
		if (_array.empty())
		{
			out += "[]";
		}
		else if (IsInlineArray(_array))
		{
			out.push_back('[');
			for (size_t i = 0; i < _array.size(); ++i)
			{
				if (i > 0)
				{
					out += ", ";
				}
				_array[i].DumpTo(out, indent);
			}
			out.push_back(']');
		}
		else
		{
			out += "[\n";
			for (size_t i = 0; i < _array.size(); ++i)
			{
				AppendIndent(out, indent + 1);
				_array[i].DumpTo(out, indent + 1);
				out += i + 1 < _array.size() ? ",\n" : "\n";
			}
			AppendIndent(out, indent);
			out.push_back(']');
		}
		break;

	case Type::Object:
		if (_object.empty())
		{
			out += "{}";
			break;
		}
		out += "{\n";
		for (size_t i = 0; i < _object.size(); ++i)
		{
			AppendIndent(out, indent + 1);
			AppendEscaped(out, _object[i].first);
			out += ": ";
			_object[i].second.DumpTo(out, indent + 1);
			out += i + 1 < _object.size() ? ",\n" : "\n";
		}
		AppendIndent(out, indent);
		out.push_back('}');
		break;
	}
}

bool Json::Parse(const std::string& text, Json& out, std::string& error)
{
	// UTF-8 BOM 허용
	if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0)
	{
		const std::string withoutBom = text.substr(3); // Parser가 참조로 들고 있으므로 이름 있는 변수로 유지
		Parser parser(withoutBom);
		return parser.Run(out, error);
	}

	Parser parser(text);
	return parser.Run(out, error);
}
