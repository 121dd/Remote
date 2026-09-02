#include "../screen_protocol.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void Expect(bool condition, const char* message){
    if(!condition){
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

ScreenUpdateHeader Header(int type, int x, int y, int width, int height, int image_length){
    ScreenUpdateHeader header{};
    header.frame_type = type;
    header.screen_width = 1920;
    header.screen_height = 1080;
    header.x = x;
    header.y = y;
    header.width = width;
    header.height = height;
    header.image_length = image_length;
    return header;
}

void TestValidFullAndDirtyUpdates(){
    const ScreenUpdateHeader full = Header(SCREEN_FRAME_FULL, 0, 0, 1920, 1080, 100);
    Expect(IsValidScreenUpdate(full, sizeof(ScreenUpdateHeader) + 100), "full update must be accepted");

    const ScreenUpdateHeader dirty = Header(SCREEN_FRAME_DIRTY, 64, 128, 256, 64, 50);
    Expect(IsValidScreenUpdate(dirty, sizeof(ScreenUpdateHeader) + 50), "bounded dirty update must be accepted");
}

void TestNoChangeHasNoImage(){
    const ScreenUpdateHeader unchanged = Header(SCREEN_FRAME_UNCHANGED, 0, 0, 0, 0, 0);
    Expect(IsValidScreenUpdate(unchanged, sizeof(ScreenUpdateHeader)), "metadata-only unchanged frame must be accepted");

    const ScreenUpdateHeader malformed = Header(SCREEN_FRAME_UNCHANGED, 0, 0, 0, 0, 1);
    Expect(!IsValidScreenUpdate(malformed, sizeof(ScreenUpdateHeader) + 1), "unchanged frame must reject image bytes");
}

void TestMalformedUpdatesAreRejected(){
    const ScreenUpdateHeader outside = Header(SCREEN_FRAME_DIRTY, 1900, 1000, 64, 100, 10);
    Expect(!IsValidScreenUpdate(outside, sizeof(ScreenUpdateHeader) + 10), "rectangle outside screen must be rejected");

    const ScreenUpdateHeader wrong_length = Header(SCREEN_FRAME_DIRTY, 0, 0, 64, 64, 10);
    Expect(!IsValidScreenUpdate(wrong_length, sizeof(ScreenUpdateHeader) + 9), "mismatched PNG length must be rejected");

    const ScreenUpdateHeader partial_full = Header(SCREEN_FRAME_FULL, 0, 0, 64, 64, 10);
    Expect(!IsValidScreenUpdate(partial_full, sizeof(ScreenUpdateHeader) + 10), "full frame must cover the whole screen");
}

void TestResourceExhaustingUpdatesAreRejected(){
    ScreenUpdateHeader too_wide = Header(SCREEN_FRAME_FULL, 0, 0, 1920, 1080, 10);
    too_wide.screen_width = SCREEN_MAX_DIMENSION + 1;
    too_wide.width = too_wide.screen_width;
    Expect(!IsValidScreenUpdate(too_wide, sizeof(ScreenUpdateHeader) + 10), "oversized dimensions must be rejected");

    ScreenUpdateHeader too_many_pixels = Header(SCREEN_FRAME_FULL, 0, 0, 8192, 8192, 10);
    too_many_pixels.screen_width = 8192;
    too_many_pixels.screen_height = 8192;
    Expect(!IsValidScreenUpdate(too_many_pixels, sizeof(ScreenUpdateHeader) + 10), "oversized canvas allocation must be rejected");

    ScreenUpdateHeader too_much_png = Header(
        SCREEN_FRAME_DIRTY, 0, 0, 64, 64, SCREEN_MAX_IMAGE_BYTES + 1);
    Expect(!IsValidScreenUpdate(
        too_much_png,
        sizeof(ScreenUpdateHeader) + static_cast<std::size_t>(SCREEN_MAX_IMAGE_BYTES) + 1),
        "oversized PNG payload must be rejected");
}

} // namespace

int main(){
    TestValidFullAndDirtyUpdates();
    TestNoChangeHasNoImage();
    TestMalformedUpdatesAreRejected();
    TestResourceExhaustingUpdatesAreRejected();
    std::cout << "screen protocol tests passed\n";
    return 0;
}
