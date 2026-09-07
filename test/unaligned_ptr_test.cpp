// nonstd::unaligned_ptr / unaligned_ref / unaligned_span - the proxy pointer, reference and view.

#include "support.hpp"

#include <nonstd/unaligned_ptr.hpp>

#include <boost/ut.hpp>

#include <algorithm>
#include <array>
#include <iterator>
#include <numeric>
#include <ranges>
#include <vector>

#include <cstring>

using namespace boost::ut;
using namespace nonstd;
using namespace std::string_view_literals;

using const_pointer = unaligned_little_ptr<const std::uint16_t>;
using mutable_pointer = unaligned_little_ptr<std::uint16_t>;

// ---------------------------------------------------------------------------------------------
// The iterator contract, checked at compile time.

static_assert(std::random_access_iterator<const_pointer>);
static_assert(std::random_access_iterator<mutable_pointer>);
static_assert(std::random_access_iterator<unaligned_big_ptr<const std::int32_t, 24>>);
static_assert(std::output_iterator<mutable_pointer, std::uint16_t>);
static_assert(!std::output_iterator<const_pointer, std::uint16_t>);
static_assert(std::sortable<mutable_pointer>);
static_assert(std::sortable<unaligned_big_ptr<std::int64_t, 40>>);
static_assert(std::sized_sentinel_for<mutable_pointer, mutable_pointer>);
static_assert(std::ranges::random_access_range<unaligned_little_span<const std::uint16_t>>);
static_assert(std::ranges::sized_range<unaligned_little_span<std::uint16_t>>);
static_assert(std::ranges::view<unaligned_little_span<const std::uint16_t>>);

// Constness lives in T: const dereferences to a value, mutable to a proxy that writes through.
static_assert(std::same_as<decltype(*std::declval<const_pointer>()), std::uint16_t>);
static_assert(std::same_as<decltype(*std::declval<mutable_pointer>()), unaligned_little_ref<std::uint16_t>>);
static_assert(std::convertible_to<mutable_pointer, const_pointer>);
static_assert(!std::convertible_to<const_pointer, mutable_pointer>);
static_assert(std::convertible_to<unaligned_little_span<std::uint16_t>, unaligned_little_span<const std::uint16_t>>);
static_assert(!std::convertible_to<unaligned_little_span<const std::uint16_t>, unaligned_little_span<std::uint16_t>>);

// A proxy pointer is exactly a pointer; nothing is materialised alongside it.
static_assert(std::is_trivially_copyable_v<const_pointer>);
static_assert(sizeof(const_pointer) == sizeof(void *));
static_assert(unaligned_big_ptr<std::int32_t, 24>::stride == 3);
static_assert(unaligned_big_ptr<std::uint64_t, 40>::stride == 5);

namespace {

/** Writes and reads at every misalignment the target could present, proving nothing spills. */
template<typename T, std::size_t Bits, std::endian E>
void round_trip_at_every_skew(T value, const char *what) {
    using pointer = unaligned_ptr<T, Bits, E>;
    constexpr auto stride = pointer::stride;
    const char *const order = E == std::endian::big ? "big" : "little";

    alignas(8) std::array<std::byte, 32> buffer {};
    // Starting at 1 so there is always a preceding byte to prove the store did not spill backwards.
    for (std::size_t skew = 1; skew <= 8; ++skew) {
        buffer.fill(std::byte { 0xA5 });
        const pointer p { buffer.data() + skew };
        *p = value;
        expect(eq(static_cast<T>(*p), value)) << what << order << "read back through the proxy, skew" << skew;
        expect(eq(*unaligned_ptr<const T, Bits, E> { p }, value))
                << what << order << "read through the const pointer, skew" << skew;
        expect(eq(std::to_integer<int>(buffer[skew - 1]), 0xA5)) << what << order << "byte before, skew" << skew;
        expect(eq(std::to_integer<int>(buffer[skew + stride]), 0xA5)) << what << order << "byte after, skew" << skew;
    }
}

template<typename T, std::size_t Bits = sizeof(T) * 8>
void round_trip(T value, const char *what) {
    round_trip_at_every_skew<T, Bits, std::endian::little>(value, what);
    round_trip_at_every_skew<T, Bits, std::endian::big>(value, what);
}

// A trivially copyable aggregate is a value type too; the byte order of its members is its own.
struct Point {
    std::int16_t x;
    std::int16_t y;
    friend bool operator==(Point, Point) = default;
};

suite<"pointer"> pointer = [] {
    "round trip at every misalignment"_test = [] {
        // Values whose bytes all differ, so a byte order or offset slip changes the result.
        round_trip<std::uint8_t>(0xC7, "uint8");
        round_trip<std::int8_t>(-42, "int8");
        round_trip<std::uint16_t>(0xBEEF, "uint16");
        round_trip<std::int16_t>(-12345, "int16");
        round_trip<std::uint32_t>(0xDEADBEEF, "uint32");
        round_trip<std::int32_t>(-1234567, "int32");
        round_trip<std::uint64_t>(0x0123456789ABCDEFull, "uint64");
        round_trip<std::int64_t>(-1234567890123456789ll, "int64");
        round_trip<float>(-2.5f, "float");
        round_trip<double>(3.141592653589793, "double");

        // Odd widths, where the storage is narrower than the value type.
        round_trip<std::uint32_t, 24>(0xABCDEF, "uint24");
        round_trip<std::int32_t, 24>(-0x123456, "int24");
        round_trip<std::int32_t, 24>(-8388608, "int24 minimum");
        round_trip<std::uint64_t, 40>(0xABCDEF0123ull, "uint40");
        round_trip<std::int64_t, 40>(-1, "int40 sign extension");
        round_trip<std::uint64_t, 48>(0xABCDEF012345ull, "uint48");
        round_trip<std::int64_t, 56>(-0x0123456789ABll, "int56");
    };

    "byte order on the wire"_test = [] {
        // A heap buffer: no alignment promise at all, which is the usual case for a packet.
        std::vector<std::byte> buffer(8);
        *unaligned_little_ptr<std::uint32_t> { buffer.data() + 1 } = 0x01020304;
        expect(eq(hex({ buffer.data() + 1, 4 }), "04 03 02 01"sv));
        *unaligned_big_ptr<std::uint32_t> { buffer.data() + 1 } = 0x01020304;
        expect(eq(hex({ buffer.data() + 1, 4 }), "01 02 03 04"sv));
        expect(eq(*unaligned_little_ptr<const std::uint32_t> { buffer.data() + 1 }, 0x04030201u))
                << "the same bytes read through the other order are reversed";

        *unaligned_big_ptr<std::int32_t, 24> { buffer.data() + 5 } = -2;
        expect(eq(hex({ buffer.data() + 5, 3 }), "ff ff fe"sv));

        const auto field = from_hex("80 00 00 00 01"sv);
        expect(eq(*unaligned_big_ptr<const std::int64_t, 40> { field.data() }, -549755813887ll));
        expect(eq(*unaligned_little_ptr<const std::int64_t, 40> { field.data() }, 0x0100000080ll));
    };

    "arithmetic moves by the stride"_test = [] {
        using pointer = unaligned_big_ptr<std::int32_t, 24>;
        std::array<std::byte, 32> buffer {};
        const pointer p { buffer.data() };

        expect(eq((p + 2).data(), buffer.data() + 6));
        expect(eq((2 + p).data(), buffer.data() + 6));
        expect(eq((p + 5 - 3).data(), buffer.data() + 6));
        expect(eq((p + 5) - p, 5));
        expect(eq(std::distance(p, p + 4), 4));

        pointer q = p;
        expect(eq((++q).data(), buffer.data() + 3));
        expect(eq((q++).data(), buffer.data() + 3)) << "post-increment yields the old position";
        expect(eq(q.data(), buffer.data() + 6));
        q -= 1;
        q += -1;
        expect(q == p);
        expect(p < p + 1 and p + 1 > p and p <= p);
        expect((p <=> p + 1) == std::strong_ordering::less);
        expect((p <=> p) == std::strong_ordering::equal);
        expect((p + 1 <=> p) == std::strong_ordering::greater);

        *(p + 2) = 7;
        expect(eq(static_cast<std::int32_t>(p[2]), 7)) << "subscript is dereference at an offset";
        expect(eq(hex({ buffer.data() + 6, 3 }), "00 00 07"sv));

        expect(eq(pointer {}.data(), static_cast<std::byte *>(nullptr))) << "default constructed is null";
        expect(pointer {} == pointer {});
    };

    "aggregate elements and operator->"_test = [] {
        const std::array<Point, 3> points { { { 1, 2 }, { 3, 4 }, { 5, 6 } } };
        std::array<std::byte, 16> buffer {};
        std::memcpy(buffer.data() + 1, points.data(), sizeof points);

        const unaligned_ptr<const Point> p { buffer.data() + 1 };
        expect(eq(p->x, 1));
        expect(eq((p + 2)->y, 6));
        expect(eq(p[1].x, 3));
        expect(*(p + 1) == Point { 3, 4 });

        const unaligned_ptr<Point> m { buffer.data() + 1 };
        *m = Point { 9, 8 };
        expect(eq(m->x, 9)) << "operator-> reads a copy; writes go through *p";
        expect(eq(static_cast<Point>(*m).y, 8));
        expect(eq(p->x, 9)) << "the const view sees the write";
    };
};

suite<"reference"> reference = [] {
    "writes through the proxy"_test = [] {
        std::array<std::byte, 16> buffer {};
        const unaligned_little_ptr<std::uint16_t> p { buffer.data() + 3 };

        *p = 100;
        expect(eq(static_cast<std::uint16_t>(*p), 100));
        const std::uint16_t plain = *p;
        expect(eq(plain, 100)) << "implicit conversion to T";
        expect(eq((*p).data(), buffer.data() + 3));

        *p += 5;
        expect(eq(static_cast<std::uint16_t>(*p), 105));
        *p -= 1;
        *p *= 2;
        *p /= 4;
        expect(eq(static_cast<std::uint16_t>(*p), 52));
        *p %= 10;
        *p |= 0b1000;
        *p ^= 0b0001;
        *p &= 0b1110;
        expect(eq(static_cast<std::uint16_t>(*p), 10));
        *p <<= 3;
        *p >>= 1;
        expect(eq(static_cast<std::uint16_t>(*p), 40));

        ++*p;
        --*p;
        expect(eq((*p)++, 40)) << "post-increment yields the old value";
        expect(eq(static_cast<std::uint16_t>(*p), 41));
        expect(eq((*p)--, 41));
        expect(eq(static_cast<std::uint16_t>(*p), 40));
        expect(eq(hex({ buffer.data() + 3, 2 }), "28 00"sv));

        p[1] = 7;
        expect(eq(static_cast<std::uint16_t>(p[1]), 7));
        expect(eq(static_cast<std::uint16_t>(*p), 40)) << "subscript did not disturb element 0";
    };

    "a reference is a binding, not a value"_test = [] {
        std::array<std::byte, 16> buffer {};
        const unaligned_little_ptr<std::uint16_t> p { buffer.data() };
        const unaligned_little_ptr<std::uint16_t> q { buffer.data() + 2 };
        *p = 1;
        *q = 2;

        *p = *q;
        expect(eq(static_cast<std::uint16_t>(*p), 2)) << "assigning a reference copies the value";
        *q = 3;
        expect(eq(static_cast<std::uint16_t>(*p), 2)) << "and not the binding";

        // As with vector<bool>, auto deduces the proxy, and writing to it writes to the buffer.
        auto r = *p;
        r = 5;
        expect(eq(static_cast<std::uint16_t>(*p), 5));

        swap(*p, *q);
        expect(eq(static_cast<std::uint16_t>(*p), 3));
        expect(eq(static_cast<std::uint16_t>(*q), 5));
    };

    "arithmetic wraps at the storage width"_test = [] {
        std::array<std::byte, 8> buffer {};
        const unaligned_big_ptr<std::int32_t, 24> s { buffer.data() };
        *s = 8388607;
        ++*s;
        expect(eq(static_cast<std::int32_t>(*s), -8388608));

        const unaligned_little_ptr<std::uint32_t, 24> u { buffer.data() + 3 };
        *u = 0;
        --*u;
        expect(eq(static_cast<std::uint32_t>(*u), 0xFFFFFFu));
        expect(eq(hex({ buffer.data(), 6 }), "80 00 00 ff ff ff"sv));
    };
};

suite<"span"> span = [] {
    "shape"_test = [] {
        std::array<std::byte, 32> buffer {};
        const unaligned_big_span<std::int32_t, 24> span { buffer.data() + 1, 5 };
        expect(eq(span.size(), 5u));
        expect(eq(span.size_bytes(), 15u));
        expect(not span.empty());
        expect(eq(span.end() - span.begin(), 5));
        expect(eq(span.bytes().size(), 15u));
        expect(eq(span.bytes().data(), buffer.data() + 1)) << "bytes() aliases the buffer";

        const unaligned_big_span<std::int32_t, 24> from_pointer { span.begin() + 1, 2 };
        expect(eq(from_pointer.bytes().data(), buffer.data() + 4));

        const unaligned_big_span<std::int32_t, 24> empty;
        expect(empty.empty());
        expect(eq(empty.size(), 0u));
        expect(empty.begin() == empty.end());
    };

    "element access through view_interface"_test = [] {
        std::array<std::byte, 32> buffer {};
        const unaligned_big_span<std::uint16_t> span { buffer.data() + 1, 4 };
        span[0] = 0x0102;
        span[3] = 0x0708;
        span.front() += 1;
        span.back() -= 1;
        expect(eq(static_cast<std::uint16_t>(span.front()), 0x0103));
        expect(eq(static_cast<std::uint16_t>(span.back()), 0x0707));
        expect(eq(hex({ buffer.data() + 1, 8 }), "01 03 00 00 00 00 07 07"sv));

        const unaligned_big_span<const std::uint16_t> readable = span;
        expect(eq(readable[3], 0x0707)) << "the const view yields values";
        expect(eq(readable.size(), 4u));
    };

    "subranges clamp instead of running off the end"_test = [] {
        std::array<std::byte, 32> buffer {};
        const unaligned_little_span<std::uint16_t> span { buffer.data() + 1, 8 };
        std::iota(span.begin(), span.end(), std::uint16_t { 0 });

        expect(eq(span.subspan(2).size(), 6u));
        expect(eq(static_cast<std::uint16_t>(span.subspan(2).front()), 2));
        expect(eq(span.subspan(2, 3).size(), 3u));
        expect(eq(span.first(3).size(), 3u));
        expect(eq(static_cast<std::uint16_t>(span.first(3).back()), 2));
        expect(eq(static_cast<std::uint16_t>(span.last(2).front()), 6));

        expect(eq(span.subspan(99).size(), 0u));
        expect(eq(span.subspan(4, 99).size(), 4u));
        expect(eq(span.first(99).size(), 8u));
        expect(eq(span.last(99).size(), 8u));
    };

    "from raw bytes"_test = [] {
        using const_span = unaligned_little_span<const std::uint16_t>;
        const auto buffer = from_hex("01 00 02 00 03 00 ff"sv);
        const std::span<const std::byte> raw = buffer;

        expect(eq(const_span::decode_TMP(raw).size(), 3u)) << "truncates to whole elements";
        expect(eq(const_span::decode_TMP(raw).back(), 3));
        expect(not const_span::try_from_bytes(raw).has_value()) << "rejects a partial element";
        expect(const_span::try_from_bytes(raw.first(6)).has_value()) << "accepts an exact multiple";
        expect(eq(const_span::try_from_bytes(raw.first(6))->size(), 3u));
        expect(eq(const_span::try_from_bytes(raw.first(0))->size(), 0u)) << "zero is an exact multiple";
    };
};

suite<"algorithms"> algorithms = [] {
    "standard algorithms through the proxy"_test = [] {
        std::array<std::byte, 32> buffer {};
        // Big endian and misaligned, so the iteration cannot accidentally be a plain array walk.
        const unaligned_big_span<std::uint16_t> span { buffer.data() + 1, 8 };

        std::iota(span.begin(), span.end(), std::uint16_t { 10 });
        expect(eq(hex({ buffer.data() + 1, 16 }), "00 0a 00 0b 00 0c 00 0d 00 0e 00 0f 00 10 00 11"sv));

        std::ranges::reverse(span);
        expect(eq(static_cast<std::uint16_t>(span[0]), 17)) << "ranges::reverse via iter_swap";
        std::ranges::sort(span);
        expect(eq(static_cast<std::uint16_t>(span[0]), 10)) << "ranges::sort";
        expect(std::ranges::is_sorted(span));
        std::ranges::rotate(span, span.begin() + 3);
        expect(eq(static_cast<std::uint16_t>(span[0]), 13)) << "ranges::rotate via iter_move";
        std::sort(span.begin(), span.end(), std::greater {});
        expect(eq(static_cast<std::uint16_t>(span[0]), 17)) << "classic std::sort via swap";

        const std::vector<std::uint16_t> values { 1, 2, 3, 4, 5, 6, 7, 8 };
        std::ranges::copy(values, span.begin());
        expect(std::ranges::equal(span, values)) << "copy in, compare against a container";

        expect(eq(std::ranges::count(span, 3), 1));
        expect(std::ranges::find(span, 5) == span.begin() + 4);
        expect(std::ranges::binary_search(span, 6));
        expect(std::ranges::lower_bound(span, 6) == span.begin() + 5);
        expect(eq(static_cast<std::uint16_t>(*std::ranges::max_element(span)), 8));
        expect(eq(std::ranges::fold_left(span, 0, std::plus {}), 36));

        // Range-for deduces the proxy, so mutation in the loop lands in the buffer.
        for (auto element : span)
            element *= 2;
        expect(eq(hex({ buffer.data() + 1, 4 }), "00 02 00 04"sv));

        std::ranges::fill(span, std::uint16_t { 0xFFFF });
        expect(eq(std::ranges::count(span, 0xFFFF), 8));
    };

    "copies out when a copy is wanted"_test = [] {
        std::array<std::byte, 32> buffer {};
        const unaligned_little_span<std::uint16_t> span { buffer.data() + 1, 8 };
        std::iota(span.begin(), span.end(), std::uint16_t { 10 });
        const unaligned_little_span<const std::uint16_t> readable = span;

        const auto copied = std::ranges::to<std::vector<std::uint16_t>>(readable);
        expect(eq(copied.size(), 8u));
        expect(eq(copied.at(4), 14));

        const auto doubled = std::ranges::to<std::vector<int>>(
                readable | std::views::transform([](std::uint16_t v) { return v * 2; }));
        expect(eq(doubled.at(0), 20));

        std::vector<std::uint16_t> target(8);
        std::ranges::copy(readable, target.begin());
        expect(std::ranges::equal(target, readable));
    };

    "24-bit samples"_test = [] {
        // Little-endian signed 24-bit PCM: the width no integer type has.
        const auto pcm = from_hex("00 00 00  ff ff 7f  00 00 80  fe ff ff  01 00 00"sv);
        const auto samples = unaligned_little_span<const std::int32_t, 24>::try_from_bytes(pcm);
        expect(samples.has_value());
        expect(eq(samples->size(), 5u));
        expect(not unaligned_little_span<const std::int32_t, 24>::try_from_bytes(std::span { pcm }.first(14))
                        .has_value())
                << "a torn last sample is rejected";
        expect(std::ranges::equal(*samples, std::vector { 0, 8388607, -8388608, -2, 1 }));
        expect(eq(std::ranges::max(*samples), 8388607));
        expect(eq(std::ranges::min(*samples), -8388608));
        expect(eq(std::ranges::fold_left(*samples, 0, std::plus {}), -2));

        // Halve the gain in place, through a mutable view of the same bytes.
        auto scratch = pcm;
        const unaligned_little_span<std::int32_t, 24> mutable_samples { scratch.data(), 5 };
        for (auto sample : mutable_samples)
            sample /= 2;
        expect(std::ranges::equal(mutable_samples, std::vector { 0, 4194303, -4194304, -1, 0 }));
        expect(eq(hex(scratch), "00 00 00 ff ff 3f 00 00 c0 ff ff ff 00 00 00"sv));
    };
};

} // namespace

// Run from main rather than from the runner's destructor at exit, so that a coverage build has
// flushed nothing yet when the tests execute, and so that the binary takes UT's command line.
int main(int argc, const char **argv) {
    return boost::ut::cfg<>.run({ .report_errors = true, .argc = argc, .argv = argv }) ? 1 : 0;
}
