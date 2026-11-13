#pragma once

// Opens folders, files and web pages in their Windows default program. Each returns an error
// message, or "" when Windows accepted the request.
namespace Open
{
	std::string Folder(const std::filesystem::path& a_folder);
	std::string File(const std::filesystem::path& a_file);
	std::string Url(std::string_view a_url);  // https:// only
}
