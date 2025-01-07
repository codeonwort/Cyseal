#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "core/vec3.h"
#include "world/camera.h"

static std::wstring ToString(const vec3& v)
{
	wchar_t buf[256];
	swprintf_s(buf, L"(%.3f, %.3f, .%3f)", v.x, v.y, v.z);
	return std::wstring(buf);
}

namespace UnitTest
{
	TEST_CLASS(TestCamera)
	{
	public:
		TEST_METHOD(FrustumCulling)
		{
			Camera camera;
			camera.lookAt(vec3(50.0f, 0.0f, 30.0f), vec3(50.0f, 0.0f, 1.0f), vec3(0.0f, 1.0f, 0.0f));
			camera.perspective(70.0f, 1.0f, 0.1f, 10000.0f);

			CameraFrustum frustum = camera.getFrustum();

			AABB inBoxes[] = {
				AABB::fromCenterAndHalfSize(vec3(50.0f, 0.0f, 5.0f), vec3(1.0f)),
				AABB::fromCenterAndHalfSize(vec3(30.0f, 10.0f, -1005.0f), vec3(10.0f)),
			};

			AABB outBoxes[] = {
				AABB::fromCenterAndHalfSize(vec3(-500.0f, 0.0f, 5.0f), vec3(10.0f)),
				AABB::fromCenterAndHalfSize(vec3(30.0f, -2000.0f, -1005.0f), vec3(50.0f)),
			};

			for (size_t i = 0; i < _countof(inBoxes); ++i)
			{
				bool bIntersects = frustum.intersectsAABB(inBoxes[i]);
				Assert::IsTrue(bIntersects);
			}
			for (size_t i = 0; i < _countof(outBoxes); ++i)
			{
				bool bIntersects = frustum.intersectsAABB(outBoxes[i]);
				Assert::IsFalse(bIntersects);
			}
		}
	};
}
