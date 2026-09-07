# unaligned

[![CI](https://github.com/nebkat/cpp-unaligned/actions/workflows/ci.yml/badge.svg?branch=ci)](https://github.com/nebkat/cpp-unaligned/actions/workflows/ci.yml)
[![Coverage Status](https://coveralls.io/repos/github/nebkat/cpp-unaligned/badge.svg?branch=ci)](https://coveralls.io/github/nebkat/cpp-unaligned?branch=ci)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/23)

Fields with a fixed byte layout, for any trivially copyable type: integers of any byte width,
floats, enums, and structs of those, in either byte order. A struct of such fields has alignment 1
and no padding, so it *is* the wire format. Standard C++23, no pragmas, and nothing is ever read
through a misaligned pointer.

## Answering an ARP request, before

Packed structs, byte arrays for anything without a native width, `ntohs` on every read, `htons`
on every write, and a misaligned `std::uint32_t` that strict-alignment targets will fault on:

```cpp
#pragma pack(push, 1)
struct ethernet_header { std::uint8_t destination[6], source[6]; std::uint16_t type; };
struct arp_packet {
    std::uint16_t hardware_type, protocol_type;
    std::uint8_t hardware_length, protocol_length;
    std::uint16_t operation;
    std::uint8_t sender_mac[6]; std::uint32_t sender_ip;
    std::uint8_t target_mac[6]; std::uint32_t target_ip;
};
struct arp_frame { ethernet_header ethernet; arp_packet arp; };
#pragma pack(pop)

std::optional<arp_frame> reply_to(arp_frame f, const std::uint8_t (&our_mac)[6], std::uint32_t our_ip) {
    if (ntohs(f.ethernet.type) != 0x0806 || ntohs(f.arp.operation) != 1 || ntohl(f.arp.target_ip) != our_ip)
        return std::nullopt;
    std::memcpy(f.ethernet.destination, f.ethernet.source, 6);
    std::memcpy(f.ethernet.source, our_mac, 6);
    f.arp.operation = htons(2);
    std::memcpy(f.arp.target_mac, f.arp.sender_mac, 6);
    f.arp.target_ip = f.arp.sender_ip;
    std::memcpy(f.arp.sender_mac, our_mac, 6);
    f.arp.sender_ip = htonl(our_ip);
    return f;
}
```

## After

Each field declares its own width and byte order once. A MAC address is a 48-bit integer, the
EtherType and operation are enums, and every field reads and writes as a plain value:

```cpp
#include <nonstd/unaligned.hpp>

using mac_address = nonstd::unaligned_big<std::uint64_t, 48>;
enum class ether_type : std::uint16_t { ipv4 = 0x0800, arp = 0x0806 };
enum class arp_operation : std::uint16_t { request = 1, reply = 2 };

struct ethernet_header { mac_address destination, source; nonstd::unaligned_big<ether_type> type; };
struct arp_packet {
    nonstd::unaligned_big_uint16_t hardware_type, protocol_type;
    nonstd::unaligned_big_uint8_t hardware_length, protocol_length;
    nonstd::unaligned_big<arp_operation> operation;
    mac_address sender_mac; nonstd::unaligned_big_uint32_t sender_ip;
    mac_address target_mac; nonstd::unaligned_big_uint32_t target_ip;
};
struct arp_frame { ethernet_header ethernet; arp_packet arp; };
static_assert(sizeof(arp_frame) == 42 && alignof(arp_frame) == 1);

std::optional<arp_frame> reply_to(arp_frame f, std::uint64_t our_mac, std::uint32_t our_ip) {
    if (f.ethernet.type != ether_type::arp || f.arp.operation != arp_operation::request || f.arp.target_ip != our_ip)
        return std::nullopt;
    f.ethernet.destination = f.ethernet.source;
    f.ethernet.source = our_mac;
    f.arp.operation = arp_operation::reply;
    f.arp.target_mac = f.arp.sender_mac;
    f.arp.target_ip = f.arp.sender_ip;
    f.arp.sender_mac = our_mac;
    f.arp.sender_ip = our_ip;
    return f;
}
```

`arp_frame` is 42 bytes in memory exactly as on the wire: `std::memcpy` it in from a received buffer
and out to a send buffer. Fields of widths no built-in type has, such as 24, 40 or 48 bits, are
values too, sign-extended when signed. Everything is `constexpr`, so protocol constants can be
`static_assert`ed. And because each field knows its own order, the same code is correct on a
big-endian host.

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
