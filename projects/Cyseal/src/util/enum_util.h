#pragma once

#define ENUM_CLASS_FLAGS(EnumType) \
	inline EnumType operator| (EnumType x, EnumType y) { return (EnumType)((__underlying_type(EnumType))x | (__underlying_type(EnumType))y); }
