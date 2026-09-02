#include "../dirty_matrix.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void Expect(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::vector<unsigned char> Frame(int width, int height, unsigned char value = 0){
    return std::vector<unsigned char>(width * height * 4, value);
}

void SetPixel(std::vector<unsigned char>& frame, int width, int x, int y, unsigned char value){
    frame[(y * width + x) * 4] = value;
}

void TestIdenticalFramesHaveNoDirtyRegion(){
    const int width = 128;
    const int height = 128;
    const auto previous = Frame(width, height);
    const auto current = previous;

    const DirtyRegion result = FindDirtyRegion(
        previous.data(), current.data(), width, height, width * 4, 64, 40);

    Expect(!result.has_changes, "identical frames must not produce an update");
    Expect(!result.full_frame, "identical frames must not become a full frame");
}

void TestOneChangedTileProducesAlignedRegion(){
    const int width = 192;
    const int height = 128;
    const auto previous = Frame(width, height);
    auto current = previous;
    SetPixel(current, width, 70, 10, 255);

    const DirtyRegion result = FindDirtyRegion(
        previous.data(), current.data(), width, height, width * 4, 64, 40);

    Expect(result.has_changes, "a changed pixel must produce an update");
    Expect(!result.full_frame, "one changed tile must stay a partial update");
    Expect(result.x == 64 && result.y == 0, "dirty region must start at its tile origin");
    Expect(result.width == 64 && result.height == 64, "dirty region must cover one tile");
    Expect(result.dirty_tiles == 1 && result.total_tiles == 6, "dirty tile counts must be reported");
}

void TestEdgeTileIsClampedToScreen(){
    const int width = 130;
    const int height = 70;
    const auto previous = Frame(width, height);
    auto current = previous;
    SetPixel(current, width, 129, 69, 1);

    const DirtyRegion result = FindDirtyRegion(
        previous.data(), current.data(), width, height, width * 4, 64, 90);

    Expect(result.x == 128 && result.y == 64, "edge tile origin must be aligned");
    Expect(result.width == 2 && result.height == 6, "edge tile must be clipped to the screen");
}

void TestLargeDirtyAreaFallsBackToFullFrame(){
    const int width = 128;
    const int height = 128;
    const auto previous = Frame(width, height);
    auto current = previous;
    SetPixel(current, width, 1, 1, 1);
    SetPixel(current, width, 65, 1, 1);

    const DirtyRegion result = FindDirtyRegion(
        previous.data(), current.data(), width, height, width * 4, 64, 40);

    Expect(result.has_changes && result.full_frame, "at least 40 percent dirty area must use a full frame");
    Expect(result.x == 0 && result.y == 0, "full frame must start at the origin");
    Expect(result.width == width && result.height == height, "full frame must cover the screen");
}

void TestMissingPreviousFrameForcesFullFrame(){
    const int width = 100;
    const int height = 50;
    const auto current = Frame(width, height);

    const DirtyRegion result = FindDirtyRegion(
        nullptr, current.data(), width, height, width * 4, 64, 40);

    Expect(result.has_changes && result.full_frame, "the first captured frame must be complete");
    Expect(result.width == width && result.height == height, "first frame dimensions must match the screen");
}

} // namespace

int main(){
    TestIdenticalFramesHaveNoDirtyRegion();
    TestOneChangedTileProducesAlignedRegion();
    TestEdgeTileIsClampedToScreen();
    TestLargeDirtyAreaFallsBackToFullFrame();
    TestMissingPreviousFrameForcesFullFrame();
    std::cout << "dirty matrix tests passed\n";
    return 0;
}
