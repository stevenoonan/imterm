#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <chrono>

using namespace std::chrono;
using namespace std::literals;

#include "imgui.h"
#include "toml.hpp"

namespace imterm {


    void CaptureWindowCreate(void);

    void PortSelectionWindow(ImGuiID id);

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

    template<typename T>
    class ComboData {

        const size_t NO_SELECTION = SIZE_MAX;

        std::vector<ComboDataItem<T>> _items;
        size_t _selected_index = NO_SELECTION;
        std::string _label;
        std::string _hidden_label;
        std::string _toml_id;
        ImGuiComboFlags _flags = 0;

    public:

        ComboData(std::string label)
            : _label(label), _hidden_label(std::string("##").append(label))
        {
            _toml_id = _label;
            std::transform(_toml_id.begin(), _toml_id.end(), _toml_id.begin(), ::tolower);
            std::replace(_toml_id.begin(), _toml_id.end(), ' ', '_');
        }

        void set_items(std::vector<ComboDataItem<T>> new_items) {
            ComboDataItem<T> * selected_item = get_selected_item_ptr(true);

            // Search through the old items and see if any of the new items have
            // the same id_str as the id_str of the selected item. If so, we
            // want to preserve the selection into the new set of items.
            if (selected_item != nullptr) {
                for (ComboDataItem<T>& new_item: new_items) {
                    if (new_item.get_display_str() == selected_item->get_display_str()) {
                        selected_item = &new_item;
                        break;
                    }
                }
            } else {
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
                    _items[_selected_index].set_is_selected(false);
                    _selected_index = NO_SELECTION;
                }
            } else if (index < _items.size()) {
                if (index != _selected_index) {
                    if (_selected_index != NO_SELECTION) {
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
            tbl->insert(_toml_id, get_selected_item().get_display_str());
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

        const char* get_label() const {
            return _label.c_str();
        }

        const char* get_hidden_label() const {
            return _hidden_label.c_str();
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

        int get_flags() const {
            return _flags;
        }

    };

}