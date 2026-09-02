#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

struct DirtyRegion{
    bool has_changes = false;
    bool full_frame = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int dirty_tiles = 0;
    int total_tiles = 0;
};

inline DirtyRegion FullFrameRegion(int width, int height, int total_tiles){
    DirtyRegion result;
    result.has_changes = width > 0 && height > 0;
    result.full_frame = result.has_changes;
    result.width = width;
    result.height = height;
    result.dirty_tiles = total_tiles;
    result.total_tiles = total_tiles;
    return result;
}

inline DirtyRegion FindDirtyRegion(
    const unsigned char* previous,
    const unsigned char* current,
    int width,
    int height,
    int stride,
    int tile_size,
    int full_frame_percent)
{
    if(current == nullptr || width <= 0 || height <= 0 || stride < width * 4 || tile_size <= 0){
        return DirtyRegion{};
    }

    const int columns = (width + tile_size - 1) / tile_size;
    const int rows = (height + tile_size - 1) / tile_size;
    const int total_tiles = columns * rows;
    if(previous == nullptr){
        return FullFrameRegion(width, height, total_tiles);
    }

    int min_column = columns;
    int min_row = rows;
    int max_column = -1;
    int max_row = -1;
    int dirty_tiles = 0;

    for(int row = 0; row < rows; ++row){
        const int tile_y = row * tile_size;
        const int tile_height = std::min(tile_size, height - tile_y);
        for(int column = 0; column < columns; ++column){
            const int tile_x = column * tile_size;
            const int tile_width = std::min(tile_size, width - tile_x);
            bool dirty = false;
            for(int y = 0; y < tile_height; ++y){
                const std::size_t offset =
                    static_cast<std::size_t>(tile_y + y) * stride +
                    static_cast<std::size_t>(tile_x) * 4;
                if(std::memcmp(previous + offset, current + offset,
                               static_cast<std::size_t>(tile_width) * 4) != 0){
                    dirty = true;
                    break;
                }
            }
            if(dirty){
                ++dirty_tiles;
                min_column = std::min(min_column, column);
                min_row = std::min(min_row, row);
                max_column = std::max(max_column, column);
                max_row = std::max(max_row, row);
            }
        }
    }

    if(dirty_tiles == 0){
        DirtyRegion result;
        result.total_tiles = total_tiles;
        return result;
    }

    DirtyRegion result;
    result.has_changes = true;
    result.x = min_column * tile_size;
    result.y = min_row * tile_size;
    result.width = std::min(width, (max_column + 1) * tile_size) - result.x;
    result.height = std::min(height, (max_row + 1) * tile_size) - result.y;
    result.dirty_tiles = dirty_tiles;
    result.total_tiles = total_tiles;

    const int threshold = std::clamp(full_frame_percent, 1, 100);
    const std::int64_t dirty_percent_numerator =
        static_cast<std::int64_t>(dirty_tiles) * 100;
    const std::int64_t bounding_percent_numerator =
        static_cast<std::int64_t>(result.width) * result.height * 100;
    const std::int64_t screen_area = static_cast<std::int64_t>(width) * height;
    if(dirty_percent_numerator >= static_cast<std::int64_t>(total_tiles) * threshold ||
       bounding_percent_numerator >= screen_area * threshold){
        result.full_frame = true;
        result.x = 0;
        result.y = 0;
        result.width = width;
        result.height = height;
    }
    return result;
}
