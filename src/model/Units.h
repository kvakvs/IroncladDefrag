#pragma once
#include <cstdint>

namespace icd {
    template <typename T, typename Tag = void>
    class Quantity {
    public:
        explicit constexpr Quantity(T val = T{}) : value(val) {}

        constexpr T getValue() const { return value; }

        constexpr Quantity& operator+=(const Quantity& rhs) {
            value += rhs.value;
            return *this;
        }
        constexpr Quantity& operator-=(const Quantity& rhs) {
            value -= rhs.value;
            return *this;
        }

        constexpr Quantity& operator*=(const Quantity& rhs) {
            value *= rhs.value;
            return *this;
        }

        constexpr Quantity& operator/=(const Quantity& rhs) {
            value /= rhs.value;
            return *this;
        }

        constexpr Quantity& operator+=(const T& rhs) {
            value += rhs;
            return *this;
        }

        constexpr Quantity& operator-=(const T& rhs) {
            value -= rhs;
            return *this;
        }

        constexpr Quantity& operator*=(const T& rhs) {
            value *= rhs;
            return *this;
        }

        constexpr Quantity& operator/=(const T& rhs) {
            value /= rhs;
            return *this;
        }

        friend constexpr bool operator==(const Quantity& lhs, const Quantity& rhs) { return lhs.value == rhs.value; }
        friend constexpr bool operator!=(const Quantity& lhs, const Quantity& rhs) { return !(lhs == rhs); }
        friend constexpr bool operator<(const Quantity& lhs, const Quantity& rhs) { return lhs.value < rhs.value; }
        friend constexpr bool operator>(const Quantity& lhs, const Quantity& rhs) { return rhs < lhs; }
        friend constexpr bool operator<=(const Quantity& lhs, const Quantity& rhs) { return !(rhs < lhs); }
        friend constexpr bool operator>=(const Quantity& lhs, const Quantity& rhs) { return !(lhs < rhs); }

        T value = {};
    };

    // Use for unit-less count of something
    struct count64_tag {};
    using count64_t = Quantity<std::uint64_t, count64_tag>;
    struct count32_tag {};
    using count32_t = Quantity<std::uint32_t, count32_tag>;

    // Use for index of something, counting from start
    struct index64_tag {};
    using index64_t = Quantity<std::uint64_t, index64_tag>;
    struct index32_tag {};
    using index32_t = Quantity<std::uint64_t, index32_tag>;

    // Use for counting bytes
    struct byte_count64_tag {};
    using byte_count64_t = Quantity<std::uint64_t, byte_count64_tag>;
    struct byte_count32_tag {};
    using byte_count32_t = Quantity<std::uint32_t, byte_count32_tag>;

    // Use for counting drive sectors
    struct sector_count64_tag {};
    using sector_count64_t = Quantity<std::uint64_t, sector_count64_tag>;
    struct sector_count32_tag {};
    using sector_count32_t = Quantity<std::uint32_t, sector_count32_tag>;

    // Used for performance/transfer rate in MB/s
    struct megabyte_sec_tag {};
    using megabyte_sec_t = Quantity<double, megabyte_sec_tag>;

} // namespace icd
