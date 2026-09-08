# nonstd::unaligned

[![CI](https://github.com/nebkat/cpp-unaligned/actions/workflows/ci.yml/badge.svg?branch=ci)](https://github.com/nebkat/cpp-unaligned/actions/workflows/ci.yml)
[![Coverage Status](https://coveralls.io/repos/github/nebkat/cpp-unaligned/badge.svg?branch=ci)](https://coveralls.io/github/nebkat/cpp-unaligned?branch=ci)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/23)

Modern C++ unaligned data storage for any trivially copyable type. Standard C++23, no pragmas, no undefined behavior.

## Why

Wire formats often compress structs for efficiency, resulting in fields resting out of their natural alignment.
Other times they use non-native endianness for compatibility with other systems.

This has traditionally been solved with the help of:
- `std::memcpy`
- `__attribute__((packed))` / `[[gnu:packed]]`
- `ntohs` / `htons`
- `reinterpret_cast`
- Macros, unions and other hacks

These methods:
- Make it hard to understand the protocol
- Are prone to encoding errors
- Often rely on undefined behavior
- May not be portable across all systems

__`nonstd::unaligned<T>` abstracts away this problem by providing a type that behaves similar to a `T` but with `alignof(nonstd::unaligned<T>) == 1`.__

```c++
// Before, nghttp2/lib/nghttp2_frame.c
void nghttp2_frame_pack_frame_hd(uint8_t *buf, const nghttp2_frame_hd *hd) {
  nghttp2_put_uint32be(&buf[0], (uint32_t)(hd->length << 8));
  buf[3] = hd->type;
  buf[4] = hd->flags;
  nghttp2_put_uint32be(&buf[5], (uint32_t)hd->stream_id);
}

void nghttp2_frame_unpack_frame_hd(nghttp2_frame_hd *hd, const uint8_t *buf) {
  *hd = (nghttp2_frame_hd){
    .length = nghttp2_get_uint32(&buf[0]) >> 8,
    .stream_id = nghttp2_get_uint32(&buf[5]) & NGHTTP2_STREAM_ID_MASK,
    .type = buf[3],
    .flags = buf[4],
  };
}
// The << 8 and >> 8 pair exists purely because there is no 24-bit type. That is your feature, stated as a workaround by someone else.

// After
enum class frame_type : std::uint8_t { data = 0, headers = 1, settings = 4, goaway = 7 };

struct frame_header {
    nonstd::unaligned_big_uint24_t length;   // uint32_t packed into 24 bits
    nonstd::unaligned_big<frame_type> type;
    nonstd::unaligned_big_uint8_t flags;
    nonstd::unaligned_big_uint32_t stream_id; // top bit reserved
};
static_assert(sizeof(frame_header) == 9 && alignof(frame_header) == 1);

```

## Packed arrays

`<nonstd/unaligned_ptr.hpp>` adds a proxy pointer and span over packed values. They model
`std::random_access_iterator`, so `<algorithm>` and `<ranges>` work straight on a byte buffer:

```cpp
nonstd::unaligned_little_span<std::int32_t, 24> samples { pcm.data(), pcm.size() / 3 };
std::ranges::sort(samples);
const auto peak = std::ranges::max(samples);
for (auto sample : samples) sample /= 2;
```

## Using it

Two headers under `include/`, nothing to build. With CMake:

```cmake
FetchContent_Declare(unaligned GIT_REPOSITORY https://github.com/nebkat/cpp-unaligned.git GIT_TAG main)
FetchContent_MakeAvailable(unaligned)
target_link_libraries(your_target PRIVATE unaligned::unaligned)
```

Tested on GCC 14, Clang 18, AppleClang and MSVC across x86_64 and arm64 Linux, macOS and Windows,
under AddressSanitizer and UndefinedBehaviorSanitizer. `cmake --build` and `ctest` run the tests.
