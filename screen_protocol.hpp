#pragma once

#include <cstddef>
#include <cstdint>

enum ScreenFrameType : std::int32_t{
    SCREEN_FRAME_UNCHANGED = 0,
    SCREEN_FRAME_FULL = 1,
    SCREEN_FRAME_DIRTY = 2
};

constexpr std::int32_t SCREEN_MAX_DIMENSION = 16384;
constexpr std::int64_t SCREEN_MAX_PIXELS = 33554432; //约等于一张 8K 画布。
constexpr std::int32_t SCREEN_MAX_IMAGE_BYTES = 10 * 1024 * 1024 - 1024;

#pragma pack(push, 1)
struct ScreenRequest{
    std::int32_t force_full;
};

struct ScreenUpdateHeader{
    std::int32_t frame_type; //类型
    std::int32_t screen_width;
    std::int32_t screen_height;
    std::int32_t x;
    std::int32_t y;
    std::int32_t width;
    std::int32_t height;
    std::int32_t image_length;
};
#pragma pack(pop)

static_assert(sizeof(ScreenRequest) == 4, "ScreenRequest is part of the wire protocol");
static_assert(sizeof(ScreenUpdateHeader) == 32, "ScreenUpdateHeader is part of the wire protocol");

//客户端收到服务器发来的屏幕更新数据后，对 ScreenUpdateHeader 做一次完整的“合法性检查”。
inline bool IsValidScreenUpdate(const ScreenUpdateHeader& header, std::size_t body_length){
    if(header.screen_width <= 0 || header.screen_height <= 0 || header.image_length < 0){
        return false;
    }
    if(header.screen_width > SCREEN_MAX_DIMENSION ||
       header.screen_height > SCREEN_MAX_DIMENSION ||
       static_cast<std::int64_t>(header.screen_width) * header.screen_height > SCREEN_MAX_PIXELS ||
       header.image_length > SCREEN_MAX_IMAGE_BYTES){
        return false;
    }
    if(body_length != sizeof(ScreenUpdateHeader) + static_cast<std::size_t>(header.image_length)){
        return false;
    }
    if(header.frame_type == SCREEN_FRAME_UNCHANGED){
        return header.x == 0 && header.y == 0 && header.width == 0 && header.height == 0 &&
               header.image_length == 0;
    }
    if(header.frame_type != SCREEN_FRAME_FULL && header.frame_type != SCREEN_FRAME_DIRTY){
        return false;
    }
    if(header.x < 0 || header.y < 0 || header.width <= 0 || header.height <= 0 ||
       header.image_length <= 0){
        return false;
    }
    const std::int64_t right = static_cast<std::int64_t>(header.x) + header.width;
    const std::int64_t bottom = static_cast<std::int64_t>(header.y) + header.height;
    if(right > header.screen_width || bottom > header.screen_height){
        return false;
    }
    if(header.frame_type == SCREEN_FRAME_FULL){
        return header.x == 0 && header.y == 0 &&
               header.width == header.screen_width && header.height == header.screen_height;
    }
    return true;
}
