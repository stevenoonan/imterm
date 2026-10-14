#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "interface_zoom.h"
#include "toml.hpp"

namespace imterm {

	class CaptureSettings
	{
		static inline const std::string DefaultConfigFileName = "imterm.toml";

		std::string mConfigFileName;
		toml::table mTable;

		toml::table CreateDefaultToml()
		{
			return toml::table {
				{ "default", toml::table {
					{ "serial", toml::table {
						{"serial_port", "COM1"},
						{ "baud", "115200" },
						{ "data_bits", "8" },
						{ "stop_bits", "2" },
						{ "parity", "none" },
						{ "flow_control", "none" }
					}},
					{ "input", toml::table {
						{"new_line_mode", "strict"}
					}},
					{ "display", toml::table {
						{"zoom_factor", 1.0}
					}}
				}}
			};
		}

		void Read()
		{
			mTable = toml::parse_file(mConfigFileName);
		}

		void Write(const toml::table& aTable)
		{
			std::ofstream configStream(mConfigFileName);
			if (configStream.is_open()) {
				configStream << aTable;
			}
		}

	public:
		explicit CaptureSettings(
			std::string aConfigFileName = DefaultConfigFileName)
			: mConfigFileName(std::move(aConfigFileName))
		{
			if (!std::filesystem::exists(mConfigFileName)) {
				Write(CreateDefaultToml());
			}
			Read();
		}

		toml::node_view<toml::node> get_serial_settings()
		{
			return mTable["default"]["serial"];
		}

		toml::node_view<toml::node> get_input_settings()
		{
			return mTable["default"]["input"];
		}

		float get_zoom_factor()
		{
			const auto value = mTable["default"]["display"]["zoom_factor"]
				.value<double>();
			return NormalizeInterfaceZoom(
				value ? static_cast<float>(*value) : DefaultInterfaceZoom);
		}

		void set_zoom_factor(float aValue)
		{
			toml::table* defaultTable = mTable["default"].as_table();
			if (!defaultTable) {
				mTable.insert_or_assign("default", toml::table{});
				defaultTable = mTable["default"].as_table();
			}

			toml::table* displayTable = (*defaultTable)["display"].as_table();
			if (!displayTable) {
				defaultTable->insert_or_assign("display", toml::table{});
				displayTable = (*defaultTable)["display"].as_table();
			}

			displayTable->insert_or_assign(
				"zoom_factor",
				static_cast<double>(NormalizeInterfaceZoom(aValue)));
			write();
		}

		void write()
		{
			Write(mTable);
		}
	};

}
