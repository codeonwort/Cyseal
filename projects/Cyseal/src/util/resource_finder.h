#pragma once

#include <vector>
#include <string>

// Given a path, searches for a matching full path.
class ResourceFinder
{

public:
	static ResourceFinder& get();

	// Register a directory
	void addBaseDirectory(const std::wstring& directory);

	// Returns empty string if not found
	std::wstring find(const std::wstring& wsubpath);

private:
	ResourceFinder();
	ResourceFinder(const ResourceFinder&) = delete;
	ResourceFinder& operator=(const ResourceFinder&) = delete;

	std::vector<std::wstring> directories;

};
