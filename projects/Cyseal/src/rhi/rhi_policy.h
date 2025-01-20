#pragma once

enum class EReverseZPolicy : uint8_t
{
	Traditional = 0,
	Reverse = 1
};
// All other codebase should consider this value.
constexpr EReverseZPolicy getReverseZPolicy()
{
	return EReverseZPolicy::Reverse;
}
constexpr float getDeviceFarDepth()
{
	return (getReverseZPolicy() == EReverseZPolicy::Reverse) ? 0.0f : 1.0f;
}
constexpr float getDeviceNearDepth()
{
	return (getReverseZPolicy() == EReverseZPolicy::Reverse) ? 1.0f : 0.0f;
}
