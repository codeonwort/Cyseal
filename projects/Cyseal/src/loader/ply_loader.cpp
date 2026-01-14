#include "ply_loader.h"
#include "util/logging.h"
#include "core/assertion.h"

#include <fstream>
#include <sstream>

DEFINE_LOG_CATEGORY_STATIC(LogPLY);

PLYMesh* PLYLoader::loadFromFile(const std::wstring& filepath)
{
	std::fstream fs(filepath, std::ios::binary | std::ios::in);
	if (!fs)
	{
		CYLOG(LogPLY, Error, L"Can't open file: %s", filepath.c_str());
		return nullptr;
	}

	PLYMesh* mesh = new PLYMesh;

	std::string magicNumber;
	fs >> magicNumber;

	if (magicNumber != "ply")
	{
		CYLOG(LogPLY, Error, L"Magic number is not 'ply': %s", filepath.c_str());
		delete mesh;
		return nullptr;
	}

	// Header properties
	bool bIsBinaryFile = false;
	std::string formatType;
	uint32 formatVersion;
	uint32 vertexCount;
	uint32 faceCount;
	uint32 sizeOfVertexIndex = 0; // In bytes
	uint32 sizeOfNumFaceVertices = 0; // In bytes
	std::vector<std::string> vertexFloatAttributes;

	std::string headerLine;
	while (std::getline(fs, headerLine))
	{
		if (headerLine == "end_header")
		{
			break;
		}
		std::stringstream ss(headerLine);
		std::string header;
		ss >> header;
		if (header == "format")
		{
			ss >> formatType >> formatVersion;
			bIsBinaryFile = formatType.starts_with("binary");
		}
		else if (header == "element")
		{
			std::string elementType;
			ss >> elementType;
			if (elementType == "vertex")
			{
				ss >> vertexCount;
			}
			else if (elementType == "face")
			{
				ss >> faceCount;
			}
			else
			{
				CYLOG(LogPLY, Error, L"Can't parse element type: %S", elementType.c_str());
			}
		}
		else if (header == "property")
		{
			std::string propertyType;
			ss >> propertyType;
			if (propertyType == "float")
			{
				std::string attrName;
				ss >> attrName;
				vertexFloatAttributes.push_back(attrName);
			}
			else if (propertyType == "list")
			{
				std::string typeOfNumFaceVertices, typeOfVertexIndex;
				ss >> typeOfNumFaceVertices >> typeOfVertexIndex;
				auto getSizeInBytes = [](const std::string& str)
				{
					if (str == "uint8") return 1;
					else if (str == "int") return 4;
					else
					{
						CYLOG(LogPLY, Error, L"Unknown property list: %S", str);
					}
					return 4;
				};
				sizeOfNumFaceVertices = getSizeInBytes(typeOfNumFaceVertices);
				sizeOfVertexIndex = getSizeInBytes(typeOfVertexIndex);
			}
		}
		else if (header.size() > 0)
		{
			CYLOG(LogPLY, Error, L"Can't parse header: %S", header.c_str());
		}
	}

	if (!bIsBinaryFile)
	{
		CYLOG(LogPLY, Error, L"Can't parse Ascii format yet: %s", filepath.c_str());
		delete mesh;
		return nullptr;
	}

	union TempVertexDecl
	{
		TempVertexDecl()
		{
			::memset(buf, 0, sizeof(buf));
		}
		struct
		{
			vec3 position;
			vec3 normal;
			vec2 texcoord;
		};
		float buf[8];
	};

	const size_t numFloatAttributes = vertexFloatAttributes.size();
	std::vector<float> floatAttrValues(numFloatAttributes);
	std::vector<uint32> floatAttrIndexMapping(numFloatAttributes);
	{
		uint32 i = 0;
		for (const std::string& attrName : vertexFloatAttributes)
		{
			if (attrName == "x") floatAttrIndexMapping[i] = 0;
			else if (attrName == "y") floatAttrIndexMapping[i] = 1;
			else if (attrName == "z") floatAttrIndexMapping[i] = 2;
			else if (attrName == "nx") floatAttrIndexMapping[i] = 3;
			else if (attrName == "ny") floatAttrIndexMapping[i] = 4;
			else if (attrName == "nz") floatAttrIndexMapping[i] = 5;
			else if (attrName == "u") floatAttrIndexMapping[i] = 6;
			else if (attrName == "v") floatAttrIndexMapping[i] = 7;
			else
			{
				CYLOG(LogPLY, Error, L"Unknown vertex attribute: %S", attrName.c_str());
			}
			++i;
		}
	}

	bool isEOF = fs.eof();

	for (uint32 vertexIx = 0; vertexIx < vertexCount; ++vertexIx)
	{
		TempVertexDecl tempVertex;
		for (size_t attrIx = 0; attrIx < numFloatAttributes; ++attrIx)
		{
			char floatBuf[4];
			fs.read(floatBuf, 4);
			float* attrVal = reinterpret_cast<float*>(floatBuf);
			tempVertex.buf[floatAttrIndexMapping[attrIx]] = *attrVal;
		}
		mesh->positionBuffer.push_back(tempVertex.position);
		mesh->normalBuffer.push_back(tempVertex.normal);
		mesh->texcoordBuffer.push_back(tempVertex.texcoord);
	}

	auto readAsUint32 = [](std::fstream& fs, uint32 elementSize) -> uint32
	{
		char buf[4];
		fs.read(buf, elementSize);
		if (elementSize == 1) return (uint32)(*reinterpret_cast<uint8*>(buf));
		else if (elementSize == 2) return (uint32)(*reinterpret_cast<uint16*>(buf));
		else if (elementSize == 4) return (uint32)(*reinterpret_cast<uint32*>(buf));
		else CYLOG(LogPLY, Error, L"readAsUint32 unknown size: %u", elementSize);
		return 0;
	};

	CHECK(sizeOfNumFaceVertices <= 4);
	CHECK(sizeOfVertexIndex <= 4);
	for (uint32 faceIx = 0; faceIx < faceCount; ++faceIx)
	{
		uint32 numFaceVertices = readAsUint32(fs, sizeOfNumFaceVertices);
		if (numFaceVertices == 3)
		{
			// #todo-pbrt-geom: Fixup winding
			uint32 i0 = readAsUint32(fs, sizeOfVertexIndex);
			uint32 i1 = readAsUint32(fs, sizeOfVertexIndex);
			uint32 i2 = readAsUint32(fs, sizeOfVertexIndex);
			mesh->indexBuffer.push_back(i0);
			mesh->indexBuffer.push_back(i1);
			mesh->indexBuffer.push_back(i2);
		}
		else if (numFaceVertices == 4)
		{
			/*
			* v0 - v1    v0 - v1
			* |     | or |     | ?
			* v2 - v3    v3 - v2
			*/
			uint32 i0 = readAsUint32(fs, sizeOfVertexIndex);
			uint32 i1 = readAsUint32(fs, sizeOfVertexIndex);
			uint32 i2 = readAsUint32(fs, sizeOfVertexIndex);
			uint32 i3 = readAsUint32(fs, sizeOfVertexIndex);
			mesh->indexBuffer.push_back(i0);
			mesh->indexBuffer.push_back(i1);
			mesh->indexBuffer.push_back(i3);
			mesh->indexBuffer.push_back(i1);
			mesh->indexBuffer.push_back(i2);
			mesh->indexBuffer.push_back(i3);
		}
	}

	return mesh;
}
