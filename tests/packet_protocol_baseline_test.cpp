#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

constexpr std::int32_t kMagic = 0x55AA77CC;

#pragma pack(push, 1)
struct LegacyPacketHeader {
    std::int32_t magic;
    std::int32_t command;
    std::int32_t body_length;
};
#pragma pack(pop)

enum class LegacyParseStatus { Incomplete, Complete, Invalid };

struct LegacyParseResult {
    LegacyParseStatus status{LegacyParseStatus::Incomplete};
    std::size_t prefix_length{0};
    std::size_t packet_length{0};
    std::vector<std::uint8_t> packet;
};

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::vector<std::uint8_t> BuildLegacy(
    std::int32_t command,
    const std::vector<std::uint8_t>& body) {
    LegacyPacketHeader header{
        kMagic, command, static_cast<std::int32_t>(body.size())};
    std::vector<std::uint8_t> bytes(sizeof(header) + body.size());
    std::memcpy(bytes.data(), &header, sizeof(header));
    if (!body.empty()) {
        std::memcpy(bytes.data() + sizeof(header), body.data(), body.size());
    }
    return bytes;
}

LegacyParseResult ParseLegacy(
    const std::uint8_t* buffer,
    std::size_t length) {
    LegacyParseResult result;
    if (buffer == nullptr && length != 0) {
        result.status = LegacyParseStatus::Invalid;
        return result;
    }

    std::size_t prefix = 0;
    LegacyPacketHeader header{};
    bool found = false;
    for (; prefix + sizeof(kMagic) <= length; ++prefix) {
        std::int32_t candidate = 0;
        std::memcpy(&candidate, buffer + prefix, sizeof(candidate));
        if (candidate == kMagic) {
            found = true;
            break;
        }
    }
    if (!found || length - prefix < sizeof(header)) {
        return result;
    }

    std::memcpy(&header, buffer + prefix, sizeof(header));
    if (header.body_length < 0) {
        result.status = LegacyParseStatus::Invalid;
        return result;
    }
    const auto body_length = static_cast<std::size_t>(header.body_length);
    const auto packet_length = sizeof(header) + body_length;
    if (length - prefix < packet_length) {
        return result;
    }

    result.status = LegacyParseStatus::Complete;
    result.prefix_length = prefix;
    result.packet_length = packet_length;
    result.packet.assign(
        buffer + prefix, buffer + prefix + packet_length);
    return result;
}

void TestEnvelopeBytesStayCompatible() {
    const std::vector<std::uint8_t> body{0x10, 0x20, 0x30};
    const auto bytes = BuildLegacy(2, body);
    const std::vector<std::uint8_t> expected{
        0xCC, 0x77, 0xAA, 0x55,
        0x02, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00,
        0x10, 0x20, 0x30};
    Expect(bytes == expected, "envelope bytes must remain unchanged");
}

void TestZeroBodyIsACompletePacket() {
    const auto bytes = BuildLegacy(1, {});
    const auto result = ParseLegacy(bytes.data(), bytes.size());
    Expect(result.status == LegacyParseStatus::Complete,
           "zero-body packet must be complete");
    Expect(result.packet_length == 12,
           "zero-body packet length must equal its header");
}

void TestPartialHeaderAndBodyStayIncomplete() {
    const auto bytes = BuildLegacy(4, {1, 2, 3, 4});
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        const auto result = ParseLegacy(bytes.data(), length);
        Expect(result.status == LegacyParseStatus::Incomplete,
               "every packet prefix must remain incomplete");
    }
}

void TestAdjacentPacketsConsumeOnlyTheFirst() {
    const auto first = BuildLegacy(1, {7});
    const auto second = BuildLegacy(2, {8, 9});
    std::vector<std::uint8_t> joined = first;
    joined.insert(joined.end(), second.begin(), second.end());

    const auto result = ParseLegacy(joined.data(), joined.size());
    Expect(result.status == LegacyParseStatus::Complete,
           "the first adjacent packet must parse");
    Expect(result.packet_length == first.size(),
           "parsing must consume exactly the first packet");
    Expect(result.packet == first, "the parsed bytes must be the first packet");
}

void TestLeadingGarbageIsLocated() {
    const auto packet = BuildLegacy(2026, {});
    std::vector<std::uint8_t> bytes{0x01, 0x02, 0x03};
    bytes.insert(bytes.end(), packet.begin(), packet.end());

    const auto result = ParseLegacy(bytes.data(), bytes.size());
    Expect(result.status == LegacyParseStatus::Complete,
           "a packet after leading garbage must be found");
    Expect(result.prefix_length == 3,
           "the parser must identify the leading garbage length");
    Expect(result.packet == packet, "leading garbage must not enter the packet");
}

} // namespace

int main() {
    static_assert(sizeof(LegacyPacketHeader) == 12,
                  "legacy packet header is part of the wire protocol");
    TestEnvelopeBytesStayCompatible();
    TestZeroBodyIsACompletePacket();
    TestPartialHeaderAndBodyStayIncomplete();
    TestAdjacentPacketsConsumeOnlyTheFirst();
    TestLeadingGarbageIsLocated();
    std::cout << "legacy packet baseline tests passed\n";
}

