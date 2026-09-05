#include "../packet_protocol.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void TestBuildProducesLegacyCompatibleBytes() {
    const std::vector<std::uint8_t> body{0x10, 0x20, 0x30};
    const auto packet =
        remote::PacketBuffer::Build(remote::Command::Mouse, body.data(), body.size());
    const std::vector<std::uint8_t> expected{
        0xCC, 0x77, 0xAA, 0x55,
        0x02, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00,
        0x10, 0x20, 0x30};

    Expect(packet.size() == expected.size(), "packet size must match legacy bytes");
    Expect(std::vector<std::uint8_t>(packet.data(), packet.data() + packet.size()) == expected,
           "packet bytes must remain legacy-compatible");
    Expect(packet.header().body_length == 3, "header must expose the body length");
    Expect(packet.body()[0] == 0x10, "body must begin after the header");
}

void TestZeroBodyPacketIsSupported() {
    const auto packet =
        remote::PacketBuffer::Build(remote::Command::Screen, nullptr, 0);
    Expect(packet.size() == sizeof(remote::PacketHeader),
           "zero-body packet must contain only a header");

    const auto result = remote::ParsePacket(packet.data(), packet.size());
    Expect(result.status == remote::ParseStatus::Complete,
           "zero-body packet must parse");
    Expect(result.packet.has_value(), "complete parse must own a packet");
    Expect(result.packet_length == sizeof(remote::PacketHeader),
           "zero-body consumed length must be one header");
}

void TestEveryPartialPrefixIsIncomplete() {
    const std::vector<std::uint8_t> body{1, 2, 3, 4};
    const auto packet =
        remote::PacketBuffer::Build(remote::Command::Keyboard, body.data(), body.size());

    for (std::size_t length = 0; length < packet.size(); ++length) {
        const auto result = remote::ParsePacket(packet.data(), length);
        Expect(result.status == remote::ParseStatus::Incomplete,
               "every partial packet prefix must remain incomplete");
        Expect(!result.packet.has_value(), "incomplete input must not allocate a packet");
    }
}

void TestAdjacentPacketsConsumeExactlyOne() {
    const std::uint8_t first_body[]{7};
    const std::uint8_t second_body[]{8, 9};
    const auto first =
        remote::PacketBuffer::Build(remote::Command::Screen, first_body, sizeof(first_body));
    const auto second =
        remote::PacketBuffer::Build(remote::Command::Mouse, second_body, sizeof(second_body));

    std::vector<std::uint8_t> joined(first.data(), first.data() + first.size());
    joined.insert(joined.end(), second.data(), second.data() + second.size());

    const auto result = remote::ParsePacket(joined.data(), joined.size());
    Expect(result.status == remote::ParseStatus::Complete,
           "first adjacent packet must parse");
    Expect(result.discarded_prefix == 0, "valid stream must discard no prefix");
    Expect(result.packet_length == first.size(), "only the first packet must be consumed");
    Expect(result.packet->header().command ==
               static_cast<std::int32_t>(remote::Command::Screen),
           "the first packet command must be preserved");
}

void TestLeadingGarbageIsReportedSeparately() {
    const auto packet =
        remote::PacketBuffer::Build(remote::Command::Test, nullptr, 0);
    std::vector<std::uint8_t> bytes{0x01, 0x02, 0x03};
    bytes.insert(bytes.end(), packet.data(), packet.data() + packet.size());

    const auto result = remote::ParsePacket(bytes.data(), bytes.size());
    Expect(result.status == remote::ParseStatus::Complete,
           "packet following garbage must parse");
    Expect(result.discarded_prefix == 3,
           "leading garbage must be reported for accumulator removal");
    Expect(result.packet_length == packet.size(),
           "packet length must exclude discarded garbage");
}

void TestMalformedLengthsAreRejected() {
    remote::PacketHeader negative{
        remote::kPacketMagic,
        static_cast<std::int32_t>(remote::Command::Screen),
        -1};
    auto negative_result = remote::ParsePacket(
        reinterpret_cast<const std::uint8_t*>(&negative), sizeof(negative));
    Expect(negative_result.status == remote::ParseStatus::Invalid,
           "negative body length must be invalid");

    remote::PacketHeader oversized{
        remote::kPacketMagic,
        static_cast<std::int32_t>(remote::Command::Screen),
        remote::kMaxPacketBodyBytes + 1};
    auto oversized_result = remote::ParsePacket(
        reinterpret_cast<const std::uint8_t*>(&oversized), sizeof(oversized));
    Expect(oversized_result.status == remote::ParseStatus::Invalid,
           "oversized body length must be invalid");
}

void TestInvalidBuildArgumentsAreRejected() {
    bool null_rejected = false;
    try {
        static_cast<void>(
            remote::PacketBuffer::Build(remote::Command::Mouse, nullptr, 1));
    } catch (const std::invalid_argument&) {
        null_rejected = true;
    }
    Expect(null_rejected, "null non-empty body must be rejected");

    bool oversized_rejected = false;
    std::uint8_t byte = 0;
    try {
        static_cast<void>(remote::PacketBuffer::Build(
            remote::Command::Mouse,
            &byte,
            static_cast<std::size_t>(remote::kMaxPacketBodyBytes) + 1));
    } catch (const std::length_error&) {
        oversized_rejected = true;
    }
    Expect(oversized_rejected, "oversized build must be rejected");
}

void TestNullInputContract() {
    const auto empty = remote::ParsePacket(nullptr, 0);
    Expect(empty.status == remote::ParseStatus::Incomplete,
           "empty null input must be incomplete");

    const auto invalid = remote::ParsePacket(nullptr, 1);
    Expect(invalid.status == remote::ParseStatus::Invalid,
           "non-empty null input must be invalid");
}

} // namespace

int main() {
    static_assert(sizeof(remote::PacketHeader) == 12,
                  "packet header is part of the wire protocol");
    TestBuildProducesLegacyCompatibleBytes();
    TestZeroBodyPacketIsSupported();
    TestEveryPartialPrefixIsIncomplete();
    TestAdjacentPacketsConsumeExactlyOne();
    TestLeadingGarbageIsReportedSeparately();
    TestMalformedLengthsAreRejected();
    TestInvalidBuildArgumentsAreRejected();
    TestNullInputContract();
    std::cout << "packet protocol tests passed\n";
}

