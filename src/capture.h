#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include "serial/serial.h"

using namespace std::chrono;
using namespace std::literals;
using namespace serial;

#include "imgui.h"
#include "toml.hpp"

namespace imterm {


    void CaptureWindowCreate(void);

    void PortSelectionWindow(ImGuiID id);

    void ReconnectionWindow(ImGuiID id);

    void CloseSerialPort();

    void OpenSerialPort(const std::string& port, uint32_t baudrate, serial::Timeout timeout,
        bytesize_t bytesize, parity_t parity, stopbits_t stopbits,
        flowcontrol_t flowcontrol);

    void Menu();

    template<typename T>
    class ComboDataItem {

        std::string _id_str;
        std::string _display_str;
        bool _is_selected;
        const T& _data;

    public:
        ComboDataItem(std::string id_str, std::string display_str, bool is_selected, T& data)
            : _id_str(id_str), _display_str(display_str), _is_selected(is_selected), _data(data)
        {}

        ComboDataItem(std::string display_str, bool is_selected, T& data)
            : _id_str(display_str), _display_str(display_str), _is_selected(is_selected), _data(data)
        {}

        bool operator==(const ComboDataItem<T>& other) const {
            return &_data == &other._data;
        }

        const std::string& get_id_str() const {
            return _id_str;
        }

        const std::string& get_display_str() const {
            return _display_str;
        }

        const char * get_display_c_str() const {
            return _display_str.c_str();
        }

        bool get_is_selected() const {
            return _is_selected;
        }

        void set_is_selected(bool val) {
            _is_selected = val;
        }

        const T& get_data() const {
            return _data;
        }

    };

    class LabeledInput {

    protected: 
        float _input_x_position;
        float _input_width;
        std::string _label;
        std::string _hidden_label;
        std::string _toml_id;
        ImGuiComboFlags _flags = 0;
        std::string _tool_tip;

    public:

        LabeledInput(std::string label, float input_x_position=200, float input_width = 200, std::string tool_tip = std::string()) :
            _input_x_position(input_x_position),
            _input_width(input_width),
            _label(label),
            _hidden_label(std::string("##").append(label)),
            _toml_id(label),
            _tool_tip(tool_tip)
        {
            std::transform(_toml_id.begin(), _toml_id.end(), _toml_id.begin(), ::tolower);
            std::replace(_toml_id.begin(), _toml_id.end(), ' ', '_');
        }


        float get_input_x_position() const {
            return _input_x_position;
        }

        void set_input_x_position(float new_input_x_position) {
            _input_x_position = new_input_x_position;
        }

        float get_input_width() const {
            return _input_width;
        }

        void set_input_width(float new_input_width) {
            _input_width = new_input_width;
        }

        std::string get_tool_tip() const {
            return _tool_tip;
        }

        int get_flags() const {
            return _flags;
        }

        const char* get_label() const {
            return _label.c_str();
        }

        const char* get_hidden_label() const {
            return _hidden_label.c_str();
        }


    };

    class ComboDataBase: public LabeledInput {
    protected:

        const size_t NO_SELECTION = SIZE_MAX;
        size_t _selected_index = NO_SELECTION;

    public:

        ComboDataBase(std::string label, float input_x_position=200, float input_width=200, std::string tool_tip = std::string()) : LabeledInput(label, input_x_position, input_width, tool_tip) {}


        virtual size_t size() const = 0;

        virtual int get_selected_index() const = 0;

        virtual void set_selected_index(size_t index) = 0;

        virtual size_t set_selected_item(toml::node_view<toml::node> node) = 0;

        virtual void put_selected_item(toml::table* tbl) = 0;

        virtual bool index_is_valid(size_t index) const = 0;

        virtual bool item_is_selected() const = 0;

        virtual const char* get_preview_value() const = 0;

    };

    template<typename T>
    class ComboData : public ComboDataBase {


        std::vector<ComboDataItem<T>> _items;

    public:

        ComboData(std::string label, float input_x_position = 200, float input_width = 200, std::string tool_tip = std::string())
            : ComboDataBase(label, input_x_position, input_width, tool_tip)
        {

        }

        void set_items(std::vector<ComboDataItem<T>> new_items) {
            ComboDataItem<T> * selected_item = get_selected_item_ptr(true);

            // Search through the old items and see if any of the new items have
            // the same id_str as the id_str of the selected item. If so, we
            // want to preserve the selection into the new set of items.
            if (selected_item != nullptr) {

                // If we don't find the equivalent item in the new list, selected_item will remain nullptr.
                ComboDataItem<T>* was_selected_item = selected_item;
                selected_item = nullptr;

                for (ComboDataItem<T>& new_item: new_items) {
                    if (new_item.get_display_str() == was_selected_item->get_display_str()) {
                        // Equivalent item is found, so mark as selected.
                        selected_item = &new_item;
                        break;
                    }
                }
            } 
            
            if (selected_item == nullptr) {

                // If there is no selected item this means we can set a default
                // Search through the list and ensure at most one item is
                // selected.
                for (ComboDataItem<T>& new_item: new_items) {
                    if (new_item.get_is_selected()) {
                        if (selected_item == nullptr) {
                            selected_item = &new_item;
                        } else {
                            // We already found a selected item, so deselect all
                            // remaining items
                            new_item.set_is_selected(false);
                        }
                    }
                }
            }

            _items.swap(new_items);
            
            // Passing nullptr is ok
            set_selected_item(selected_item);
        }

        T& operator[](size_t index) {
            return _items[index];
        }

        size_t size() const {
            return _items.size();
        }

        const std::vector<ComboDataItem<T>> get_items() const {
            return _items;
        }

        int get_selected_index() const {
            return _selected_index;
        }

        void set_selected_index(size_t index) {
            if (index == NO_SELECTION) {
                if (_selected_index != NO_SELECTION) {
                    if (index_is_valid(_selected_index)) {
                        _items[_selected_index].set_is_selected(false);
                    }
                    _selected_index = NO_SELECTION;
                }
            } else if (index < _items.size()) {
                if (index != _selected_index) {
                    if (_selected_index != NO_SELECTION && index_is_valid(_selected_index)) {
                        _items[_selected_index].set_is_selected(false);
                    }
                    _selected_index = index;
                    _items[_selected_index].set_is_selected(true);
                }
            } else {
                throw std::out_of_range("invalid input");
            }
        }

        size_t set_selected_item(ComboDataItem<T>& needle) {
            return set_selected_item(&needle);
        }

        size_t set_selected_item(ComboDataItem<T> * needle) {

            size_t new_selected_index = -1;

            if (needle != nullptr) {
                 auto it = std::find(_items.begin(), _items.end(), *needle);
                if (it != _items.end()) {
                    new_selected_index = std::distance(_items.begin(), it);
                } else {
                    throw std::out_of_range("item does not exist in the list of items");
                }
            }

            set_selected_index(new_selected_index);

            return new_selected_index;
        }

        size_t set_selected_item(toml::node_view<toml::node> node) {

            std::optional<std::string> value_str = node[_toml_id].value<std::string>();

            if (value_str.has_value()) {
                for (int i = 0; i < _items.size(); i++) {
                    if (_items[i].get_display_str() == value_str) {
                        set_selected_index(i);
                    }
                }
            }

            return 0;
        }

        void put_selected_item(toml::table * tbl) {
            tbl->insert_or_assign(_toml_id, get_selected_item().get_display_str());
        }

        ComboDataItem<T>* get_selected_item_ptr(bool null_ok=false) {
            if (item_is_selected()) {
                return &_items[_selected_index];
            } else if (null_ok) {
                return nullptr;
            } else {
                throw std::out_of_range("no item is selected");
            }
        }

        ComboDataItem<T>& get_selected_item() {
            if (item_is_selected()) {
                return _items[_selected_index];
            } else {
                throw std::out_of_range("no item is selected");
            }
        }

        bool index_is_valid(size_t index) const {
            return (index >= 0 && index < _items.size());
        }

        bool item_is_selected() const {
            return index_is_valid(_selected_index);
        }

        const char* get_preview_value() const {
            if (item_is_selected()) {
                return _items[_selected_index].get_display_c_str();
            } else {
                return "Select Value";
            }
        }

        const T& get_selected_data() const {
            if (item_is_selected()) {
                return _items[_selected_index].get_data();
            } else {
                throw std::out_of_range("no item is selected");
            }
        }

    };

    class CaptureSettings {

        static inline const std::string _default_cfg_file_name = "imterm.toml";

        std::string _cfg_file_name;
        toml::table _table;

        toml::table create_default_toml() {
            return toml::table {
                { "default", toml::table {
                    { "serial", toml::table {
                        {"serial_port", "COM1"},
                        { "baud", "115200" },
                        { "data_bits", "8" },
                        { "stop_bits", "2" },
                        { "parity", "none" },
                        { "flow_control", "none" }
                    }
                    },
                    { "input", toml::table {
                            {"new_line_mode", "strict"}
                        }
                    }
                }
                },
            };
        }

        void read() {
            _table = toml::parse_file(_cfg_file_name);            
        }

        void write(toml::table table) {
            std::ofstream cfg_stream(_cfg_file_name);
            if (cfg_stream.is_open()) {
                cfg_stream << table;
                cfg_stream.close();
            }
        }

    public:

        CaptureSettings(std::string cfg_file_name = _default_cfg_file_name) : _cfg_file_name(cfg_file_name) {
            if (!std::filesystem::exists(_cfg_file_name)) {
                auto tbl = create_default_toml();
                write(tbl);
            }
            read();
        }

        toml::node_view<toml::node> get_serial_settings() {
            return _table["default"]["serial"];
        }
        toml::node_view<toml::node> get_input_settings() {
            return _table["default"]["input"];
        }

        void write() {
            write(_table);
        }

    };

}