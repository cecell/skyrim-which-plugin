#include "Open.h"

#include <Windows.h>
#include <shellapi.h>

namespace Open
{
	namespace
	{
		std::string Shell(const std::wstring& a_target)
		{
			// ShellExecute returns a value above 32 on success.
			const auto result = reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr, L"open", a_target.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
			return result > 32 ? std::string{} : std::format("Windows couldn't open it (error {})", result);
		}
	}

	std::string Folder(const std::filesystem::path& a_folder)
	{
		std::error_code ec;
		std::filesystem::create_directories(a_folder, ec);
		if (!std::filesystem::is_directory(a_folder, ec)) {
			return std::format("{} doesn't exist", a_folder.string());
		}
		return Shell(a_folder.wstring());
	}

	std::string File(const std::filesystem::path& a_file)
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(a_file, ec)) {
			return std::format("{} doesn't exist", a_file.string());
		}
		return Shell(a_file.wstring());
	}

	std::string Url(std::string_view a_url)
	{
		if (!a_url.starts_with("https://")) {
			return "only https:// pages are opened";
		}
		return Shell(std::wstring(a_url.begin(), a_url.end()));
	}
}
