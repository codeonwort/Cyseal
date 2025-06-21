#include "resource_finder.h"

#include <filesystem>
#include <assert.h>

static inline bool fileExists(const std::wstring& path)
{
	return std::filesystem::exists(path);
}

static inline std::wstring getParentDirectory(const std::wstring& path)
{
	std::filesystem::path p(path);
	return p.parent_path().wstring();
}

ResourceFinder& ResourceFinder::get()
{
	// C++11 guarantees this is thread-safe
	static ResourceFinder instance;
	return instance;
}

ResourceFinder::ResourceFinder()
{
	directories.push_back(L"./");
}

void ResourceFinder::addBaseDirectory(const std::wstring& directory)
{
	auto last = directory.at(directory.size() - 1);
	assert(last == L'/' || last == L'\\');
	directories.push_back(directory);
}

std::wstring ResourceFinder::find(const std::wstring& subpath)
{
	for (const auto& dir : directories)
	{
		auto fullpath = dir + subpath;
		if (fileExists(fullpath))
		{
			return fullpath;
		}
	}
	return L"";
}

void ResourceFinder::find2(const std::wstring& subpath, std::wstring& outPath, std::wstring& outBaseDir)
{
	for (const auto& dir : directories)
	{
		auto fullpath = dir + subpath;
		if (fileExists(fullpath))
		{
			outPath = fullpath;
			outBaseDir = getParentDirectory(fullpath);
			return;
		}
	}
}
