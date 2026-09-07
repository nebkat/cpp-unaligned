// nonstd::unaligned - the value type: layout, byte order, widths, sign extension and arithmetic.

#include "support.hpp"

#include <nonstd/unaligned.hpp>

#include <boost/ut.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <tuple>
#include <type_traits>

#include <cstring>

using namespace boost::ut;
using namespace nonstd;
using namespace std::string_view_literals;

// ---------------------------------------------------------------------------------------------
// Layout. The whole point of the type is that it can sit in a struct that mirrors a wire format.

static_assert(sizeof(unaligned_little_uint8_t) == 1);
static_assert(sizeof(unaligned_big_uint24_t) == 3);
static_assert(sizeof(unaligned_big<std::int64_t, 40>) == 5);
static_assert(sizeof(unaligned_little_float64_t) == 8);
static_assert(alignof(unaligned_big_uint64_t) == 1);
static_assert(alignof(unaligned_little_float64_t) == 1);
static_assert(std::is_trivially_copyable_v<unaligned_big_uint64_t>);
static_assert(std::is_standard_layout_v<unaligned_big_uint64_t>);

// Constant expressions: usable in constexpr tables and static_asserts of protocol constants.
static_assert(unaligned_big_uint16_t { 0x1234 }.storage()[0] == std::byte { 0x12 });
static_assert(unaligned_little_uint16_t { 0x1234 }.storage()[0] == std::byte { 0x34 });
static_assert(unaligned_little<std::int32_t, 24> { -1 }.value() == -1);
static_assert(unaligned_big<float> { 1.0f }.value() == 1.0f);

// The common type with a plain arithmetic type is that arithmetic type, so generic code that
// asks std::common_type_t what to compute in gets an answer instead of a hard error.
static_assert(std::same_as<std::common_type_t<unaligned_big_uint16_t, int>, int>);
static_assert(std::same_as<std::common_type_t<double, unaligned_little_int32_t>, double>);
static_assert(std::same_as<std::common_type_t<unaligned_big_int16_t, unaligned_little_int64_t>, std::int64_t>);

namespace {

/** Builds a value from its wire bytes, the way a parser would. */
template<typename U>
U decode(std::string_view text) {
    const auto bytes = from_hex(text);
    expect(eq(bytes.size(), U::storage_bytes)) << "test vector has the storage width";
    U value;
    std::ranges::copy(bytes, value.data());
    return value;
}

/** Zero, one, both extremes of the *storage* width, and a value on either side of zero. */
template<typename U>
constexpr auto boundary_values() {
    using T = typename U::value_type;
    constexpr int width = static_cast<int>(std::min(U::storage_bytes, U::value_bytes) * 8);
    constexpr int surplus = static_cast<int>(sizeof(T) * 8) - width;
    constexpr T max = static_cast<T>(std::numeric_limits<T>::max() >> surplus);
    if constexpr (std::is_signed_v<T>) {
        constexpr T min = static_cast<T>(-max - 1);
        return std::array<T, 7> { 0, 1, -1, max, min, static_cast<T>(max / 3), static_cast<T>(min / 3) };
    } else {
        return std::array<T, 4> { 0, 1, max, static_cast<T>(max / 3) };
    }
}

// Every integer alias: each width takes its own path through to_storage/from_storage (same
// size as T, narrower than T, wider than T; native or reversed byte order; signed or not).
using integer_types = std::tuple<unaligned_little_uint8_t, unaligned_little_int8_t, unaligned_little_uint16_t,
        unaligned_little_int16_t, unaligned_little_uint24_t, unaligned_little_int24_t, unaligned_little_uint32_t,
        unaligned_little_int32_t, unaligned_little_uint40_t, unaligned_little_int40_t, unaligned_little_uint48_t,
        unaligned_little_int48_t, unaligned_little_uint56_t, unaligned_little_int56_t, unaligned_little_uint64_t,
        unaligned_little_int64_t, unaligned_big_uint8_t, unaligned_big_int8_t, unaligned_big_uint16_t,
        unaligned_big_int16_t, unaligned_big_uint24_t, unaligned_big_int24_t, unaligned_big_uint32_t,
        unaligned_big_int32_t, unaligned_big_uint40_t, unaligned_big_int40_t, unaligned_big_uint48_t,
        unaligned_big_int48_t, unaligned_big_uint56_t, unaligned_big_int56_t, unaligned_big_uint64_t,
        unaligned_big_int64_t, unaligned_little<std::uint8_t, 16>, unaligned_big<std::int16_t, 32>>;

// A record laid out exactly as it is on the wire. Every field has alignment 1, so there is no
// padding and the struct is the frame.
struct Header {
    unaligned_big_uint16_t magic;
    unaligned_little_uint32_t length;
    unaligned_big<std::int32_t, 24> offset;
};
static_assert(sizeof(Header) == 9);
static_assert(alignof(Header) == 1);

suite<"encoding"> encoding = [] {
    "byte layout"_test = [] {
        expect(eq(hex(unaligned_big_uint32_t { 0x01020304 }.storage()), "01 02 03 04"sv));
        expect(eq(hex(unaligned_little_uint32_t { 0x01020304 }.storage()), "04 03 02 01"sv));
        expect(eq(hex(unaligned_uint16_t { 0x1234 }.storage()),
                std::endian::native == std::endian::little ? "34 12"sv : "12 34"sv))
                << "native is whichever the host is";

        // Narrow widths keep the low-order bytes of the value; bits above the width are dropped.
        expect(eq(hex(unaligned_big_uint24_t { 0xABCDEF }.storage()), "ab cd ef"sv));
        expect(eq(hex(unaligned_little_uint24_t { 0xABCDEF }.storage()), "ef cd ab"sv));
        expect(eq(hex(unaligned_big_uint24_t { 0x12ABCDEF }.storage()), "ab cd ef"sv));
        expect(eq(hex(unaligned_little_uint40_t { 0x0102030405 }.storage()), "05 04 03 02 01"sv));
        expect(eq(hex(unaligned_big<std::uint64_t, 56> { 0x01020304050607 }.storage()), "01 02 03 04 05 06 07"sv));

        // Negative values are two's complement at the storage width.
        expect(eq(hex(unaligned_big_int24_t { -2 }.storage()), "ff ff fe"sv));
        expect(eq(hex(unaligned_little_int24_t { -2 }.storage()), "fe ff ff"sv));
        expect(eq(hex(unaligned_big_int40_t { -549755813888 }.storage()), "80 00 00 00 00"sv));

        // Floating point is the IEEE bit pattern in the requested order.
        expect(eq(hex(unaligned_big_float32_t { 1.0f }.storage()), "3f 80 00 00"sv));
        expect(eq(hex(unaligned_little_float32_t { -2.5f }.storage()), "00 00 20 c0"sv));
        expect(eq(hex(unaligned_big_float64_t { 1.5 }.storage()), "3f f8 00 00 00 00 00 00"sv));

        expect(eq(hex(unaligned_big_int24_t {}.storage()), "00 00 00"sv)) << "default is all zero";
    };

    "decoding wire bytes"_test = [] {
        // Independent of the encoder, so that a symmetric bug cannot hide behind a round trip.
        expect(eq(decode<unaligned_big_uint16_t>("12 34").value(), 0x1234));
        expect(eq(decode<unaligned_little_uint16_t>("12 34").value(), 0x3412));
        expect(eq(decode<unaligned_big_uint32_t>("de ad be ef").value(), 0xDEADBEEFu));
        expect(eq(decode<unaligned_little_uint64_t>("ef cd ab 89 67 45 23 01").value(), 0x0123456789ABCDEFull));

        // Sign extension from the top bit of the storage, not of the value type.
        expect(eq(decode<unaligned_big_int8_t>("80").value(), -128));
        expect(eq(decode<unaligned_little_int16_t>("fe ff").value(), -2));
        expect(eq(decode<unaligned_big_int24_t>("ff ff fe").value(), -2));
        expect(eq(decode<unaligned_little_int24_t>("00 00 80").value(), -8388608));
        expect(eq(decode<unaligned_big_int24_t>("7f ff ff").value(), 8388607));
        expect(eq(decode<unaligned_big_int40_t>("80 00 00 00 00").value(), -549755813888ll));
        expect(eq(decode<unaligned_little_int56_t>("ff ff ff ff ff ff ff").value(), -1ll));
        expect(eq(decode<unaligned_big_uint24_t>("ff ff fe").value(), 0xFFFFFEu)) << "unsigned never extends";

        expect(eq(decode<unaligned_big_float32_t>("c0 20 00 00").value(), -2.5f));
        expect(eq(decode<unaligned_little_float64_t>("00 00 00 00 00 00 f8 3f").value(), 1.5));
    };

    "round trip at the limits of the width"_test = []<typename U>(U) {
        using T = typename U::value_type;
        for (const T value : boundary_values<U>()) {
            const U stored = value;
            expect(eq(stored.value(), value)) << "value() of" << +value;
            expect(eq(static_cast<T>(stored), value)) << "conversion of" << +value;

            // The bytes are the whole state: copying them copies the value.
            U copy;
            copy.storage() = stored.storage();
            expect(eq(copy.value(), value)) << "storage copy of" << +value;

            U assigned;
            assigned = value;
            expect(eq(hex(assigned.storage()), hex(stored.storage()))) << "assignment of" << +value;
        }
    } | integer_types {};

    "storage wider than the value"_test = [] {
        // The value sits at the low-order end and the surplus bytes carry the sign.
        expect(eq(hex(unaligned_big<std::int16_t, 32> { -2 }.storage()), "ff ff ff fe"sv));
        expect(eq(hex(unaligned_little<std::int16_t, 32> { -2 }.storage()), "fe ff ff ff"sv));
        expect(eq(hex(unaligned_big<std::int16_t, 32> { 2 }.storage()), "00 00 00 02"sv));
        expect(eq(hex(unaligned_big<std::uint8_t, 16> { 0xAB }.storage()), "00 ab"sv));

        // Reading takes only the bytes the value has room for.
        expect(eq(decode<unaligned_big<std::int16_t, 32>>("00 00 80 00").value(), -32768));
        expect(eq(decode<unaligned_little<std::int16_t, 32>>("00 80 00 00").value(), -32768));
        expect(eq(decode<unaligned_big<std::uint8_t, 16>>("ff 12").value(), 0x12));
    };

    "floating point patterns"_test = [] {
        // Compared as bits, because == cannot see the sign of zero or the payload of a NaN.
        for (const float value : { 0.0f, -0.0f, 1.0f, -2.5f, std::numeric_limits<float>::infinity(),
                     -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::denorm_min(),
                     std::numeric_limits<float>::max(), std::numeric_limits<float>::quiet_NaN() }) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            expect(eq(std::bit_cast<std::uint32_t>(unaligned_big_float32_t { value }.value()), bits)) << "big";
            expect(eq(std::bit_cast<std::uint32_t>(unaligned_little_float32_t { value }.value()), bits)) << "little";
        }
        for (const double value : { 0.0, -0.0, 3.141592653589793, std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::lowest(),
                     std::numeric_limits<double>::quiet_NaN() }) {
            const auto bits = std::bit_cast<std::uint64_t>(value);
            expect(eq(std::bit_cast<std::uint64_t>(unaligned_big_float64_t { value }.value()), bits)) << "big";
            expect(eq(std::bit_cast<std::uint64_t>(unaligned_little_float64_t { value }.value()), bits)) << "little";
        }
    };

    "a struct that is the frame"_test = [] {
        const auto frame = from_hex("ca fe  78 56 34 12  ff ff fe"sv);
        Header header;
        std::memcpy(&header, frame.data(), sizeof header);
        expect(eq(header.magic.value(), 0xCAFE));
        expect(eq(header.length.value(), 0x12345678u));
        expect(eq(header.offset.value(), -2));

        header.magic = 0xBEEF;
        header.offset += 7;
        std::array<std::byte, sizeof header> bytes;
        std::memcpy(bytes.data(), &header, sizeof header);
        expect(eq(hex(bytes), "be ef 78 56 34 12 00 00 05"sv));
    };
};

suite<"arithmetic"> arithmetic = [] {
    "compound assignment"_test = [] {
        unaligned_big_uint16_t v = 10;
        v += 5;
        expect(eq(v.value(), 15));
        v -= 3;
        expect(eq(v.value(), 12));
        v *= 4;
        expect(eq(v.value(), 48));
        v /= 5;
        expect(eq(v.value(), 9));
        v %= 4;
        expect(eq(v.value(), 1));
        v |= 0b1100;
        expect(eq(v.value(), 13));
        v &= 0b0110;
        expect(eq(v.value(), 4));
        v ^= 0b0101;
        expect(eq(v.value(), 1));
        v <<= 7;
        expect(eq(v.value(), 128));
        v >>= 3;
        expect(eq(v.value(), 16));

        expect(eq((++v).value(), 17)) << "pre-increment yields the new value";
        expect(eq(v++, 17)) << "post-increment yields the old value";
        expect(eq(v.value(), 18));
        expect(eq((--v).value(), 17));
        expect(eq(v--, 17));
        expect(eq(v.value(), 16));

        expect(eq(v + 4, 20)) << "converts to T in plain expressions";
        expect(eq(hex(v.storage()), "00 10"sv)) << "big endian underneath throughout";
    };

    "arithmetic wraps at the storage width"_test = [] {
        unaligned_little_uint24_t u = 0xFFFFFF;
        ++u;
        expect(eq(u.value(), 0u));
        u -= 1;
        expect(eq(u.value(), 0xFFFFFFu));

        unaligned_big_int24_t s = 8388607;
        ++s;
        expect(eq(s.value(), -8388608));

        // A value wider than the width is truncated on the way in, like an integer conversion.
        const unaligned_big_int24_t t = 0x12FFFFFF;
        expect(eq(t.value(), -1));
    };

    "raw storage access"_test = [] {
        unaligned_big_uint16_t v = 0x1234;
        expect(eq(std::to_integer<int>(*v.data()), 0x12)) << "data() is the first byte";

        std::array<std::byte, 2> &raw = v;
        raw[1] = std::byte { 0x56 };
        expect(eq(v.value(), 0x1256)) << "the storage reference aliases the value";

        const std::array<std::byte, 2> snapshot = v;
        expect(eq(hex(snapshot), "12 56"sv));
    };
};

} // namespace

int main() {}
