#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "capture_settings.h"

namespace {

class TemporarySettingsFile
{
public:
	TemporarySettingsFile()
	{
		const auto suffix = std::chrono::steady_clock::now()
			.time_since_epoch().count();
		mPath = std::filesystem::temp_directory_path()
			/ ("imterm-settings-test-" + std::to_string(suffix) + ".toml");
	}

	~TemporarySettingsFile()
	{
		std::error_code error;
		std::filesystem::remove(mPath, error);
	}

	const std::filesystem::path& GetPath() const { return mPath; }

	void Write(const std::string& aContents)
	{
		std::ofstream stream(mPath);
		stream << aContents;
	}

private:
	std::filesystem::path mPath;
};

TEST(CaptureSettingsTest, NewSettingsDefaultToOneHundredPercentZoom)
{
	TemporarySettingsFile file;
	imterm::CaptureSettings settings(file.GetPath().string());

	EXPECT_FLOAT_EQ(settings.get_zoom_factor(), 1.0f);
}

TEST(CaptureSettingsTest, PersistsZoomAndLoadsTheNearestPreset)
{
	TemporarySettingsFile file;
	{
		imterm::CaptureSettings settings(file.GetPath().string());
		settings.set_zoom_factor(1.24f);
	}

	imterm::CaptureSettings reloaded(file.GetPath().string());
	EXPECT_FLOAT_EQ(reloaded.get_zoom_factor(), 1.25f);
}

TEST(CaptureSettingsTest, NormalizesANonPresetValueLoadedFromDisk)
{
	TemporarySettingsFile file;
	file.Write("[default.display]\nzoom_factor = 1.13\n");

	imterm::CaptureSettings settings(file.GetPath().string());
	EXPECT_FLOAT_EQ(settings.get_zoom_factor(), 1.1f);
}

TEST(CaptureSettingsTest, MissingAndInvalidZoomValuesFallBackToDefault)
{
	TemporarySettingsFile missing;
	missing.Write("[default.display]\n");
	imterm::CaptureSettings missing_settings(missing.GetPath().string());
	EXPECT_FLOAT_EQ(missing_settings.get_zoom_factor(), 1.0f);

	TemporarySettingsFile invalid;
	invalid.Write("[default.display]\nzoom_factor = \"large\"\n");
	imterm::CaptureSettings invalid_settings(invalid.GetPath().string());
	EXPECT_FLOAT_EQ(invalid_settings.get_zoom_factor(), 1.0f);

	TemporarySettingsFile non_positive;
	non_positive.Write("[default.display]\nzoom_factor = 0.0\n");
	imterm::CaptureSettings non_positive_settings(
		non_positive.GetPath().string());
	EXPECT_FLOAT_EQ(non_positive_settings.get_zoom_factor(), 1.0f);
}

}
