#pragma once

#include <xstring>
#include <stringapiset.h>
#include <time.h>
#include <algorithm>

namespace SString
{
	static std::string WstringToUTF8(const std::wstring& wstr)
	{
		if (wstr.empty())
		{
			return std::string();
		}
		size_t size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
		std::string result(size, 0);
		WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &result[0], size, NULL, NULL);
		return result;
	}

	static std::wstring UTF8ToWstring(const std::string& str)
	{
		if (str.empty())
		{
			return std::wstring();
		}
		size_t size = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
		std::wstring result(size, 0);
		MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &result[0], size);
		return result;
	}

	// convert UTF-8 string to wstring 
	static std::wstring StringToWstring(const std::string& str)
	{
		std::wstring result;
		size_t len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.size(), NULL, 0);
		if (len < 0)return result;
		wchar_t* buffer = new wchar_t[len + 1];
		if (buffer == NULL)return result;
		MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.size(), buffer, len);
		buffer[len] = '\0';
		result.append(buffer);
		delete[] buffer;
		return result;
	}

	// convert wstring to UTF-8 string 
	static std::string WstringToString(const std::wstring& wstr)
	{
		std::string result;
		size_t len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), NULL, 0, NULL, NULL);
		if (len <= 0)return result;
		char* buffer = new char[len + 1];
		if (buffer == NULL)return result;
		WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.size(), buffer, len, NULL, NULL);
		buffer[len] = '\0';
		result.append(buffer);
		delete[] buffer;
		return result;
	}

	constexpr uint32_t hashString32(const char* s)
	{
		uint32_t hash = 2166136261u;
		while (*s)
		{
			hash = 16777619u * (hash ^ (uint32_t)(*s++));
		}
		return hash;
	}

	constexpr uint64_t hashString64(const char* s)
	{
		uint64_t hash = 14695981039346656037llu;
		while (*s)
		{
			hash = 1099511628211llu * (hash ^ (uint64_t)(*s++));
		}
		return hash;
	}

	static inline std::string getTimeString()
	{
		time_t now = time(0);
		char nowString[100];
		ctime_s(nowString, 100, &now);
		std::string time = nowString;
		std::replace(time.begin(), time.end(), ' ', '_');
		std::replace(time.begin(), time.end(), ':', '.');
		time.pop_back(); // Pop last \n.

		return time;
	}

}