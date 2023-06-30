#pragma once

#include <iostream>
#include <vector>
#include <chrono>

template<typename T>
class vector_timed : public std::vector<T> {
private:
    std::chrono::system_clock::time_point timestamp;

    // Function to update the timestamp
    void updateTimestamp() {
        timestamp = std::chrono::system_clock::now();
    }


public:

    using typename std::vector<T>::iterator;
    using typename std::vector<T>::const_iterator;
    using typename std::vector<T>::size_type;
    using typename std::vector<T>::value_type;

    using std::vector<T>::vector;  // Inherit constructors from std::vector
    using std::vector<T>::erase;

    vector_timed() : std::vector<T>(), timestamp(std::chrono::system_clock::now()) {}

    vector_timed(const vector_timed& other)
        : std::vector<T>(other), timestamp(other.timestamp)
    {
    }


    void push_back(const T& value) {
        std::vector<T>::push_back(value);
        timestamp = std::chrono::system_clock::now();
    }

    void push_back(T&& value) {
        std::vector<T>::push_back(std::move(value));
        updateTimestamp();
    }

    void pop_back() {
        std::vector<T>::pop_back();
        updateTimestamp();
    }

    iterator insert(const_iterator position, const T& value) {
        auto it = std::vector<T>::insert(position, value);
        updateTimestamp();
        return it;
    }

    iterator insert(const_iterator position, T&& value) {
        auto it = std::vector<T>::insert(position, std::forward<T>(value));
        updateTimestamp();
        return it;
    }

    template<class InputIt>
    iterator insert(const_iterator pos, InputIt first, InputIt last) {
        auto it = std::vector<T>::insert(pos, first, last);
        updateTimestamp();
        return it;
    }

    void clear() {
        std::vector<T>::clear();
        updateTimestamp();
    }

    template <typename... Args>
    iterator emplace(const_iterator pos, Args&&... args) {
        auto it = std::vector<T>::emplace(pos, std::forward<Args>(args)...);
        timestamp = std::chrono::system_clock::now();
        return it;
    }

    iterator erase(const_iterator position) {
        auto it = std::vector<T>::erase(position);
        updateTimestamp();
        return it;
    }

    iterator erase(const_iterator first, const_iterator last) {
        auto it = std::vector<T>::erase(first, last);
        updateTimestamp();
        return it;
    }

    template <typename... Args>
    void emplace_back(Args&&... args) {
        std::vector<T>::emplace_back(std::forward<Args>(args)...);
        timestamp = std::chrono::system_clock::now();
    }

    void resize(size_type count) {
        std::vector<T>::resize(count);
        timestamp = std::chrono::system_clock::now();
    }

    void resize(size_type count, const value_type& value) {
        std::vector<T>::resize(count, value);
        timestamp = std::chrono::system_clock::now();
    }

    void swap(vector_timed& other) {
        std::vector<T>::swap(other);
        timestamp = std::chrono::system_clock::now();
    }

    vector_timed& operator=(const vector_timed& other) noexcept {
        std::vector<T>::operator=(other);
        timestamp = std::chrono::system_clock::now();
        return *this;
    }

    vector_timed& operator=(vector_timed&& other) noexcept {
        if (this != &other)
        {
            std::vector<T>::operator=(std::move(other));
            timestamp = std::move(other.timestamp);
        }
        return *this;
    }

    template <typename InputIt>
    void assign(InputIt first, InputIt last) {
        std::vector<T>::assign(first, last);
        timestamp = std::chrono::system_clock::now();
    }

    void assign(size_type count, const value_type& value) {
        std::vector<T>::assign(count, value);
        timestamp = std::chrono::system_clock::now();
    }

    std::chrono::system_clock::time_point getTimestamp() const {
        return timestamp;
    }


};
