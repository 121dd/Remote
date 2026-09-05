#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace remote {

// 线协议常量：修改这些值会影响现有客户端与服务端的兼容性。
inline constexpr std::int32_t kPacketMagic = 0x55AA77CC;
inline constexpr std::int32_t kMaxPacketBodyBytes = 10 * 1024 * 1024;

enum class Command : std::int32_t {
    Screen = 1,
    Mouse = 2,
    Keyboard = 4,
    Test = 2026,
};

#pragma pack(push, 1)
// 固定 12 字节的网络包头；使用 memcpy 读写，避免未对齐指针转换。
struct PacketHeader {
    std::int32_t magic;
    std::int32_t command;
    std::int32_t body_length;
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 12,
              "PacketHeader is part of the wire protocol");

struct ParseResult;
ParseResult ParsePacket(
    const std::uint8_t* buffer,
    std::size_t length) noexcept;

class PacketBuffer {
public:
    // 根据命令和包体构造一段连续的“包头 + 包体”网络数据。
    static PacketBuffer Build(
        Command command,
        const void* body,
        std::size_t body_length) {
        if (body_length > static_cast<std::size_t>(kMaxPacketBodyBytes) ||
            body_length >
                static_cast<std::size_t>(
                    std::numeric_limits<std::int32_t>::max())) {
            throw std::length_error("packet body is too large");
        }
        if (body == nullptr && body_length != 0) {
            throw std::invalid_argument("non-empty packet body is null");
        }

        PacketHeader header{
            kPacketMagic,
            static_cast<std::int32_t>(command),
            static_cast<std::int32_t>(body_length)};
        const std::size_t packet_size = sizeof(header) + body_length;
        auto bytes = std::unique_ptr<std::uint8_t[]>(
            new std::uint8_t[packet_size]);
        std::memcpy(bytes.get(), &header, sizeof(header));
        if (body_length != 0) {
            std::memcpy(bytes.get() + sizeof(header), body, body_length);
        }
        return PacketBuffer(header, std::move(bytes), packet_size);
    }

    // 从已接收的完整网络包复制数据，并校验包头和长度。
    static PacketBuffer CopyFrom(
        const std::uint8_t* packet,
        std::size_t packet_length) {
        if (packet == nullptr || packet_length < sizeof(PacketHeader)) {
            throw std::invalid_argument("packet bytes are incomplete");
        }

        PacketHeader header{};
        std::memcpy(&header, packet, sizeof(header));
        if (header.magic != kPacketMagic || header.body_length < 0) {
            throw std::invalid_argument("packet header is invalid");
        }
        const auto body_length = static_cast<std::size_t>(header.body_length);
        if (body_length > static_cast<std::size_t>(kMaxPacketBodyBytes) ||
            packet_length != sizeof(PacketHeader) + body_length) {
            throw std::length_error("packet length is invalid");
        }

        auto bytes = std::unique_ptr<std::uint8_t[]>(
            new std::uint8_t[packet_length]);
        std::memcpy(bytes.get(), packet, packet_length);
        return PacketBuffer(header, std::move(bytes), packet_length);
    }

    const PacketHeader& header() const noexcept {
        return header_;
    }

    const std::uint8_t* body() const noexcept {
        return bytes_.get() + sizeof(PacketHeader);
    }

    const std::uint8_t* data() const noexcept {
        return bytes_.get();
    }

    std::size_t size() const noexcept {
        return size_;
    }

private:
    // ParsePacket 已完成包头校验，可直接构造，避免再次解析同一包头。
    friend ParseResult ParsePacket(
        const std::uint8_t* buffer,
        std::size_t length) noexcept;

    PacketBuffer(
        PacketHeader header,
        std::unique_ptr<std::uint8_t[]> bytes,
        std::size_t size) noexcept
        : header_(header), bytes_(std::move(bytes)), size_(size) {}

    // 便于业务代码读取的包头副本，不是另一份待发送的网络包。
    PacketHeader header_{};

    // 唯一拥有的连续网络数据，内容是“包头 + 包体”；析构时自动释放。
    std::unique_ptr<std::uint8_t[]> bytes_;

    // bytes_ 的总字节数，即 sizeof(PacketHeader) + body_length。
    std::size_t size_{0};
};

enum class ParseStatus {
    Incomplete,
    Complete,
    Invalid,
};

struct ParseResult {
    ParseStatus status{ParseStatus::Incomplete};
    std::size_t discarded_prefix{0};
    std::size_t packet_length{0};
    std::optional<PacketBuffer> packet;
};

inline ParseResult ParsePacket(
    const std::uint8_t* buffer,
    std::size_t length) noexcept {
    ParseResult result;
    if (buffer == nullptr) {
        result.status =
            length == 0 ? ParseStatus::Incomplete : ParseStatus::Invalid;
        return result;
    }

    // TCP 字节流可能从半包或无效数据开始，逐字节寻找魔数并保留尾部。
    std::size_t prefix = 0;
    bool found_magic = false;
    while (prefix + sizeof(kPacketMagic) <= length) {
        std::int32_t candidate = 0;
        std::memcpy(&candidate, buffer + prefix, sizeof(candidate));
        if (candidate == kPacketMagic) {
            found_magic = true;
            break;
        }
        ++prefix;
    }

    if (!found_magic) {
        result.discarded_prefix =
            length > sizeof(kPacketMagic) - 1
                ? length - (sizeof(kPacketMagic) - 1)
                : 0;
        return result;
    }

    result.discarded_prefix = prefix;
    if (length - prefix < sizeof(PacketHeader)) {
        return result;
    }

    PacketHeader header{};
    std::memcpy(&header, buffer + prefix, sizeof(header));
    if (header.body_length < 0 ||
        header.body_length > kMaxPacketBodyBytes) {
        result.status = ParseStatus::Invalid;
        return result;
    }

    const auto body_length = static_cast<std::size_t>(header.body_length);
    const auto packet_length = sizeof(PacketHeader) + body_length;
    if (length - prefix < packet_length) {
        return result;
    }

    try {
        auto bytes = std::unique_ptr<std::uint8_t[]>(
            new std::uint8_t[packet_length]);
        std::memcpy(bytes.get(), buffer + prefix, packet_length);
        result.packet = PacketBuffer(
            header, std::move(bytes), packet_length);
        result.packet_length = packet_length;
        result.status = ParseStatus::Complete;
    } catch (...) {
        result.packet.reset();
        result.packet_length = 0;
        result.status = ParseStatus::Invalid;
    }
    return result;
}

} // namespace remote
