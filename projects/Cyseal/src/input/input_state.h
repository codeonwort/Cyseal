#pragma once

#include "core/int_types.h"

enum class EInputConstants : uint16
{
	UNDEFINED,

	// Keyboard
	KEYBOARD_A,
	KEYBOARD_B,
	KEYBOARD_C,
	KEYBOARD_D,
	KEYBOARD_E,
	KEYBOARD_F,
	KEYBOARD_G,
	KEYBOARD_H,
	KEYBOARD_I,
	KEYBOARD_J,
	KEYBOARD_K,
	KEYBOARD_L,
	KEYBOARD_M,
	KEYBOARD_N,
	KEYBOARD_O,
	KEYBOARD_P,
	KEYBOARD_Q,
	KEYBOARD_R,
	KEYBOARD_S,
	KEYBOARD_T,
	KEYBOARD_U,
	KEYBOARD_V,
	KEYBOARD_W,
	KEYBOARD_X,
	KEYBOARD_Y,
	KEYBOARD_Z,
	KEYBOARD_0,
	KEYBOARD_1,
	KEYBOARD_2,
	KEYBOARD_3,
	KEYBOARD_4,
	KEYBOARD_5,
	KEYBOARD_6,
	KEYBOARD_7,
	KEYBOARD_8,
	KEYBOARD_9,
	BACKTICK, // ascii = 0x60

	SHIFT,
	CTRL,
	ALT,

	KEYBOARD_ARROW_LEFT,
	KEYBOARD_ARROW_RIGHT,
	KEYBOARD_ARROW_UP,
	KEYBOARD_ARROW_DOWN,

	// Mouse
	MOUSE_LEFT_BUTTON,
	MOUSE_MIDDLE_BUTTON,
	MOUSE_RIGHT_BUTTON,

	// XboxOne Pad
	XBOXONE_A,
	XBOXONE_B,
	XBOXONE_X,
	XBOXONE_Y,
	XBOXONE_DPAD_UP,
	XBOXONE_DPAD_DOWN,
	XBOXONE_DPAD_LEFT,
	XBOXONE_DPAD_RIGHT,
	XBOXONE_START,
	XBOXONE_BACK,
	XBOXONE_LB,
	XBOXONE_LT,
	XBOXONE_RB,
	XBOXONE_RT,
	XBOXONE_L3,
	XBOXONE_R3,
	XBOXONE_LEFT_TRIGGER,
	XBOXONE_RIGHT_TRIGGER,
	XBOXONE_LEFT_THUMB_X,
	XBOXONE_LEFT_THUMB_Y,
	XBOXONE_RIGHT_THUMB_X,
	XBOXONE_RIGHT_THUMB_Y,

	NUM_CONSTANTS
};

EInputConstants virtualKeyToInputConstant(int32_t virtualKey)
{
	// A ~ Z
	if (0x41 <= virtualKey && virtualKey <= 0x5A)
	{
		return (EInputConstants)((int32)(EInputConstants::KEYBOARD_A) + (virtualKey - 0x41));
	}

	return EInputConstants::UNDEFINED;
}

struct InputState
{
	inline bool isDown(EInputConstants inputConstant) const
	{
		return states[(int32)inputConstant];
	}

	inline void reset()
	{
		constexpr int32 N = (int32)EInputConstants::NUM_CONSTANTS;
		for (int32 i = 0; i < N; ++i)
		{
			states[i] = false;
		}
	}

	bool states[(int32)EInputConstants::NUM_CONSTANTS];
};
