#pragma once

class MaterialShaderDatabase
{
public:
	static MaterialShaderDatabase& get();
private:
	MaterialShaderDatabase();
	~MaterialShaderDatabase();

public:
	void compileMaterials();

private:
	//
};
