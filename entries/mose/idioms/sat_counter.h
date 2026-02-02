#ifndef IDIOM_SAT_COUNTER_H_
#define IDIOM_SAT_COUNTER_H_

#include <cassert>

template <typename Base, Base Min, Base Max>
class sat_counter {
    Base value_;

public:
    constexpr sat_counter() : value_(0) {}
    constexpr sat_counter(Base v) : value_(v) {}

    constexpr bool operator==(const sat_counter &other) const { return value_ == other.value_; }
    constexpr bool operator!=(const sat_counter &other) const { return value_ != other.value_; }
    constexpr bool operator>=(const sat_counter &other) const { return value_ >= other.value_; }
    constexpr bool operator<=(const sat_counter &other) const { return value_ <= other.value_; }
    constexpr bool operator>(const sat_counter &other) const { return value_ > other.value_; }
    constexpr bool operator<(const sat_counter &other) const { return value_ < other.value_; }
    constexpr explicit operator bool() const { return bool(value_); }
    constexpr bool operator!() const { return !value_; }
    constexpr explicit operator Base() const { return value_; }

    constexpr Base value() const { return value_; }
    constexpr sat_counter operator++(int) { sat_counter result = *this; *this += 1; return result; }
    constexpr sat_counter operator++() { return *this += 1; }
    constexpr sat_counter operator--(int) { sat_counter result = *this; *this -= 1; return result; }
    constexpr sat_counter operator--() { return *this -= 1; }

    constexpr sat_counter &operator+=(sat_counter a) {
        Base adj = a.value_;
        if (adj < 0) {
            if (value_ < Min - adj)
                value_ = Min;
            else
                value_ += adj;
        } else {
            if (value_ > Max - adj)
                value_ = Max;
            else
                value_ += adj;
        }
        return *this;
    }
    constexpr sat_counter &operator-=(sat_counter a) {
        Base adj = a.value_;
        if (adj > Base(0)) {
            if (value_ < Min + adj)
                value_ = Min;
            else
                value_ -= adj;
        } else {
            if (value_ > Max + adj)
                value_ = Max;
            else
                value_ -= adj;
        }
        return *this;
    }

};

#endif
