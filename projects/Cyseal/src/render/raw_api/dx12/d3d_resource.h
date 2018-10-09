#pragma once

#include "render/gpu_resource.h"

class D3DResource : public GPUResource
{

public:
	inline ID3D12Resource* getRaw() const { return rawResource; }
	inline void setRaw(ID3D12Resource* raw) { rawResource = raw; }
	
protected:
	ID3D12Resource* rawResource;

};
