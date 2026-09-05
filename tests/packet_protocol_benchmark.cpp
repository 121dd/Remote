#include "../packet_protocol.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace {

constexpr std::int32_t kLegacyMagic = 0x55AA77CC;

#pragma pack(push, 1)
struct LegacyHeader {
    std::int32_t magic;
    std::int32_t command;
    std::int32_t body_length;
};
#pragma pack(pop)

struct FreeBytes {
    void operator()(std::uint8_t* bytes) const noexcept {
        std::free(bytes);
    }
};

using LegacyBytes = std::unique_ptr<std::uint8_t, FreeBytes>;

struct LegacyPacket {
    LegacyBytes bytes;
    std::size_t size{0};
};

LegacyPacket LegacyPack(
    std::int32_t command,
    const std::vector<std::uint8_t>& body) {
    const std::size_t packet_size = sizeof(LegacyHeader) + body.size();
    LegacyBytes bytes(static_cast<std::uint8_t*>(std::malloc(packet_size)));
    if (!bytes) throw std::bad_alloc();

    const LegacyHeader header{
        kLegacyMagic, command, static_cast<std::int32_t>(body.size())};
    std::memcpy(bytes.get(), &header, sizeof(header));
    if (!body.empty()) {
        std::memcpy(bytes.get() + sizeof(header), body.data(), body.size());
    }
    return {std::move(bytes), packet_size};
}

LegacyPacket LegacyParse(const std::uint8_t* buffer, std::size_t length) {
    std::size_t prefix = 0;
    bool found = false;
    for (; prefix + sizeof(kLegacyMagic) <= length; ++prefix) {
        std::int32_t candidate = 0;
        std::memcpy(&candidate, buffer + prefix, sizeof(candidate));
        if (candidate == kLegacyMagic) {
            found = true;
            break;
        }
    }
    if (!found || length - prefix < sizeof(LegacyHeader)) return {};

    LegacyHeader header{};
    std::memcpy(&header, buffer + prefix, sizeof(header));
    if (header.body_length < 0) return {};
    const auto packet_size =
        sizeof(LegacyHeader) + static_cast<std::size_t>(header.body_length);
    if (length - prefix < packet_size) return {};

    LegacyBytes bytes(static_cast<std::uint8_t*>(std::malloc(packet_size)));
    if (!bytes) throw std::bad_alloc();
    std::memcpy(bytes.get(), buffer + prefix, packet_size);
    return {std::move(bytes), packet_size};
}

std::int64_t Median(std::vector<std::int64_t> samples) {
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

std::size_t Iterations(std::size_t body_size) {
    if (body_size >= 1024 * 1024) return 96;
    if (body_size >= 4096) return 4096;
    return 200000;
}

struct Measurements {
    std::vector<std::int64_t> legacy_pack;
    std::vector<std::int64_t> modern_pack;
    std::vector<std::int64_t> legacy_parse;
    std::vector<std::int64_t> modern_parse;
    std::uint64_t checksum{0};
};

template <typename Operation>
std::int64_t Measure(std::size_t iterations, Operation operation) {
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i) operation(i);
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               end - start).count() /
           static_cast<std::int64_t>(iterations);
}

void Benchmark(std::size_t body_size) {
    const std::size_t iterations = Iterations(body_size);
    std::vector<std::uint8_t> body(body_size);
    for (std::size_t i = 0; i < body.size(); ++i) {
        body[i] = static_cast<std::uint8_t>((i * 131U + 17U) & 0xFFU);
    }

    auto legacy_source = LegacyPack(1, body);
    auto modern_source =
        remote::PacketBuffer::Build(remote::Command::Screen, body.data(), body.size());
    Measurements values;

    for (int warmup = 0; warmup < 2; ++warmup) {
        auto legacy_packet = LegacyPack(1, body);
        auto legacy_parsed = LegacyParse(legacy_source.bytes.get(), legacy_source.size);
        auto modern_packet =
            remote::PacketBuffer::Build(remote::Command::Screen, body.data(), body.size());
        auto modern_parsed =
            remote::ParsePacket(modern_source.data(), modern_source.size());
        values.checksum += legacy_packet.size + legacy_parsed.size +
                           modern_packet.size() + modern_parsed.packet_length;
    }

    for (int sample = 0; sample < 9; ++sample) {
        const auto legacy_pack = [&] {
            values.legacy_pack.push_back(Measure(iterations, [&](std::size_t i) {
                auto packet = LegacyPack(1, body);
                values.checksum += packet.bytes.get()[(i + sample) % packet.size];
            }));
        };
        const auto modern_pack = [&] {
            values.modern_pack.push_back(Measure(iterations, [&](std::size_t i) {
                auto packet = remote::PacketBuffer::Build(
                    remote::Command::Screen, body.data(), body.size());
                values.checksum += packet.data()[(i + sample) % packet.size()];
            }));
        };
        const auto legacy_parse = [&] {
            values.legacy_parse.push_back(Measure(iterations, [&](std::size_t i) {
                auto packet = LegacyParse(legacy_source.bytes.get(), legacy_source.size);
                values.checksum += packet.bytes.get()[(i + sample) % packet.size];
            }));
        };
        const auto modern_parse = [&] {
            values.modern_parse.push_back(Measure(iterations, [&](std::size_t i) {
                auto parsed =
                    remote::ParsePacket(modern_source.data(), modern_source.size());
                values.checksum +=
                    parsed.packet->data()[(i + sample) % parsed.packet->size()];
            }));
        };

        if (sample % 2 == 0) {
            legacy_pack();
            modern_pack();
            legacy_parse();
            modern_parse();
        } else {
            modern_pack();
            legacy_pack();
            modern_parse();
            legacy_parse();
        }
    }

    std::cout << "paired,size=" << body_size
              << ",legacy_pack_ns=" << Median(values.legacy_pack)
              << ",modern_pack_ns=" << Median(values.modern_pack)
              << ",legacy_parse_ns=" << Median(values.legacy_parse)
              << ",modern_parse_ns=" << Median(values.modern_parse)
              << ",checksum=" << values.checksum << '\n';
}

} // namespace

int main() {
    static_assert(sizeof(LegacyHeader) == sizeof(remote::PacketHeader));
    for (const std::size_t size : {0U, 16U, 4096U, 1048576U}) {
        Benchmark(size);
    }
}

