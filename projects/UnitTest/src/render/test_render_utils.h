#pragma once

#include "core/int_types.h"
#include "core/assertion.h"

#include <string>

namespace render_test
{
	/// <summary>
	/// Convert rgba32f image to rgba8ui image.
	/// </summary>
	/// <param name="src">rgba32f image data.</param>
	/// <param name="numPixels">Width * height.</param>
	/// <returns></returns>
	std::vector<uint8> rgba32f_to_rgba8ui(float* src, uint32 numPixels);

	/// <summary>
	/// Compare reference image against the actual render data.
	/// </summary>
	/// <param name="refImagePath">The path to reference PNG image, relative to the solution directory.</param>
	/// <param name="imageActual">Pointer to rgba8 image data.</param>
	/// <returns>The number of different pixels.</returns>
	uint32 compareRefImageToRgba8ui(const wchar_t* refImagePath, uint8* imageActual);

	/// <summary>
	/// Similar to compareRefImageToRgba8ui, but the actual data is converted to rgba8ui before comparison.
	/// </summary>
	/// <param name="refImagePath"></param>
	/// <param name="imageActual"></param>
	/// <returns></returns>
	uint32 compareRefImageToRgba32f(const wchar_t* refImagePath, float* imageActual);

	/// <summary>
	/// Save rgba8ui image as PNG.
	/// </summary>
	/// <param name="filepath"></param>
	/// <param name="rgba8Image"></param>
	/// <param name="width"></param>
	/// <param name="height"></param>
	/// <returns></returns>
	bool saveRgba8uiImage(const wchar_t* filepath, uint8* rgba8Image, uint32 width, uint32 height);

	/// <summary>
	/// Similar to saveRgba8uiImage, but the image data is converted to rgba8ui before save.
	/// </summary>
	/// <param name="filepath"></param>
	/// <param name="rgba8Image"></param>
	/// <param name="width"></param>
	/// <param name="height"></param>
	/// <returns></returns>
	bool saveRgba32fImage(const wchar_t* filepath, float* rgba32fImage, uint32 width, uint32 height);

	/// <summary>
	/// Generates all combinations of specified configs. (yeah the name includes permutation but actually it's combination)
	/// </summary>
	class ConfigPermutation
	{
	public:
		enum class ConfigType { Bool, Integer };
		struct Config
		{
			ConfigType type;
			std::string name; // Just for debugging when breakpoint hits.
			uint32 value;
		};
		struct ConfigHandle
		{
			inline bool getBoolValue(int32 ix) const
			{
				const Config& conf = (*ptr)[ix];
				CHECK(conf.type == ConfigType::Bool);
				return (bool)conf.value;
			}
			inline uint32 getIntValue(int32 ix) const
			{
				const Config& conf = (*ptr)[ix];
				CHECK(conf.type == ConfigType::Integer);
				return conf.value;
			}
			inline int32 getLinearIx() const { return *ptrLinearIx; }
			std::vector<Config>* ptr = nullptr;
			int32* ptrLinearIx = nullptr;
		};

		ConfigPermutation()
		{}

		int32 addBoolConfig(const char* name)
		{
			CHECK(!bStarted && !bFinished);

			configs.push_back({ ConfigType::Bool, name, 2 });
			return (int32)(configs.size() - 1);
		}

		int32 addIntConfig(const char* name, uint32 count)
		{
			CHECK(!bStarted && !bFinished);

			configs.push_back({ ConfigType::Integer, name, count });
			return (int32)(configs.size() - 1);
		}

		ConfigHandle init()
		{
			CHECK(!bStarted && !bFinished);
			bStarted = true;

			current.resize(configs.size());
			nTotalConfigs = 1;
			for (size_t i = 0; i < current.size(); ++i)
			{
				current[i] = Config(configs[i].type, configs[i].name.c_str(), 0);
				nTotalConfigs *= configs[i].value;
			}
			currentLinearIx = 0;
			return ConfigHandle{ &current, &currentLinearIx };
		}

		uint32 numTotalConfigs() const
		{
			CHECK(bStarted);
			return nTotalConfigs;
		}

		bool advance()
		{
			CHECK(bStarted && !bFinished);

			const size_t n = current.size();
			current[0].value += 1;
			for (size_t i = 0; i < n; ++i)
			{
				if (current[i].value == configs[i].value)
				{
					if (n > 1) current[i].value = 0;
					if (i + 1 != n) current[i + 1].value += 1;
				}
			}
			++currentLinearIx;
			bFinished = current[n - 1].value == configs[n - 1].value;
			return !bFinished;
		}

	private:
		std::vector<Config> configs;
		uint32 nTotalConfigs = 0;

		std::vector<Config> current;
		int32 currentLinearIx = -1;
		bool bStarted = false, bFinished = false;
	};
}
