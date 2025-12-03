#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>
#include <iostream>

namespace Zest {

// ----------------------------
// Core writer / reader
// ----------------------------

class binary_writer {
public:
    explicit binary_writer(std::ostream& os) : os_(os) {}

    bool write_bytes(const void* data, std::size_t n) {
        os_.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
        if (!os_) {
            return false;
        }
        return true;
    }

private:
    std::ostream& os_;

    template<typename T> friend void serialize(binary_writer&, const T&);
    template<typename T> friend void serialize_pod(binary_writer&, const T&);
};

class binary_reader {
public:
    explicit binary_reader(std::istream& is) : is_(is) {}

    bool read_bytes(void* data, std::size_t n) {
        is_.read(static_cast<char*>(data), static_cast<std::streamsize>(n));
        if (!is_) {
            return false;
        }
        return true;
    }

private:
    std::istream& is_;

    template<typename T> friend void deserialize(binary_reader&, T&);
    template<typename T> friend void deserialize_pod(binary_reader&, T&);
};

// ----------------------------
// POD helpers
// ----------------------------

template<typename T>
void serialize_pod(binary_writer& w, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "serialize_pod requires trivially copyable type");
    w.write_bytes(&v, sizeof(T));
}

template<typename T>
void deserialize_pod(binary_reader& r, T& v) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "deserialize_pod requires trivially copyable type");
    r.read_bytes(&v, sizeof(T));
}

// Default: trivially copyable => POD
template<typename T>
std::enable_if_t<std::is_trivially_copyable_v<T>>
serialize(binary_writer& w, const T& v) {
    serialize_pod(w, v);
}

template<typename T>
std::enable_if_t<std::is_trivially_copyable_v<T>>
deserialize(binary_reader& r, T& v) {
    deserialize_pod(r, v);
}

// ----------------------------
// std::string
// ----------------------------

inline void serialize(binary_writer& w, const std::string& s) {
    std::uint32_t size = static_cast<std::uint32_t>(s.size());
    serialize_pod(w, size);
    if (!s.empty()) {
        w.write_bytes(s.data(), size);
    }
}

inline void deserialize(binary_reader& r, std::string& s) {
    std::uint32_t size = 0;
    deserialize_pod(r, size);
    s.resize(size);
    if (size != 0) {
        r.read_bytes(s.data(), size);
    }
}

// ----------------------------
// std::vector<T> (nested ok)
// ----------------------------

template<typename T>
void serialize(binary_writer& w, const std::vector<T>& vec) {
    std::uint32_t size = static_cast<std::uint32_t>(vec.size());
    serialize_pod(w, size);

    if constexpr (std::is_trivially_copyable_v<T>) {
        if (!vec.empty()) {
            w.write_bytes(vec.data(), sizeof(T) * vec.size());
        }
    } else {
        for (auto const& elem : vec) {
            serialize(w, elem); // recursive
        }
    }
}

template<typename T>
void deserialize(binary_reader& r, std::vector<T>& vec) {
    std::uint32_t size = 0;
    deserialize_pod(r, size);
    vec.resize(size);

    if constexpr (std::is_trivially_copyable_v<T>) {
        if (!vec.empty()) {
            r.read_bytes(vec.data(), sizeof(T) * vec.size());
        }
    } else {
        for (auto& elem : vec) {
            deserialize(r, elem); // recursive
        }
    }
}

// ----------------------------
// User-defined types
// ----------------------------
//
// You implement:
//   void serialize(binary_writer& w, const YourType& v);
//   void deserialize(binary_reader& r, YourType& v);
// using the above building blocks.
//

} // namespace Zest
