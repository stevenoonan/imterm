#include <gtest/gtest.h>

#include <limits>

#include "interface_zoom.h"

TEST(InterfaceZoomTest, NormalizesToTheNearestPreset)
{
	EXPECT_FLOAT_EQ(imterm::NormalizeInterfaceZoom(1.13f), 1.10f);
	EXPECT_FLOAT_EQ(imterm::NormalizeInterfaceZoom(1.24f), 1.25f);
	EXPECT_FLOAT_EQ(imterm::NormalizeInterfaceZoom(0.10f), 0.50f);
	EXPECT_FLOAT_EQ(imterm::NormalizeInterfaceZoom(5.00f), 2.00f);
	EXPECT_FLOAT_EQ(
		imterm::NormalizeInterfaceZoom(
			std::numeric_limits<float>::quiet_NaN()),
		1.00f);
	EXPECT_FLOAT_EQ(imterm::NormalizeInterfaceZoom(0.00f), 1.00f);
	EXPECT_FLOAT_EQ(imterm::NormalizeInterfaceZoom(-1.00f), 1.00f);
}

TEST(InterfaceZoomTest, StepsThroughPresetsAndClampsAtTheBounds)
{
	EXPECT_FLOAT_EQ(imterm::StepInterfaceZoom(1.00f, 1), 1.10f);
	EXPECT_FLOAT_EQ(imterm::StepInterfaceZoom(1.00f, -1), 0.90f);
	EXPECT_FLOAT_EQ(imterm::StepInterfaceZoom(2.00f, 1), 2.00f);
	EXPECT_FLOAT_EQ(imterm::StepInterfaceZoom(0.50f, -1), 0.50f);
}

TEST(InterfaceZoomTest, ZeroDirectionOnlyNormalizes)
{
	EXPECT_FLOAT_EQ(imterm::StepInterfaceZoom(1.13f, 0), 1.10f);
}

TEST(InterfaceZoomTest, ResetUsesOneHundredPercent)
{
	EXPECT_FLOAT_EQ(imterm::DefaultInterfaceZoom, 1.00f);
}
