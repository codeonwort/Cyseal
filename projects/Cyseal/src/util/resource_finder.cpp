#include "resource_finder.h"
#include <assert.h>

////////////////////////////////////////////////////////
// Platform-specific
#include <Shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
static inline bool fileExists(const std::wstring& path)
{
	return PathFileExistsW(path.c_str()) == TRUE ? true : false;
}
////////////////////////////////////////////////////////

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

void ResourceFinder::add(const std::wstring& directory)
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
