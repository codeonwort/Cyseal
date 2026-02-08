#include "material_database.h"

MaterialShaderDatabase& MaterialShaderDatabase::get()
{
	static MaterialShaderDatabase instance;
	return instance;
}

MaterialShaderDatabase::MaterialShaderDatabase()
{
}

MaterialShaderDatabase::~MaterialShaderDatabase()
{
}

void MaterialShaderDatabase::compileMaterials()
{
}
