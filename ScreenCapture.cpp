#include <stdio.h>
#include <iostream>
#include <Windows.h>
#include <gdiplus.h>

using namespace Gdiplus;

//查找指定 MIME 类型的图片编码器 CLSID（GDI+ 没有 ImageFormatPNG 这种常量，要自己查）
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid){
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if(size == 0) return -1;
    ::ImageCodecInfo* pInfo = (::ImageCodecInfo*)malloc(size);
    Gdiplus::GetImageEncoders(num, size, pInfo);
    for(UINT i = 0; i < num; ++i){
        if(wcscmp(pInfo[i].MimeType, format) == 0){
            *pClsid = pInfo[i].Clsid;
            free(pInfo);
            return 0;
        }
    }
    free(pInfo);
    return -1;
}

int main(){
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    //GDI+ 初始化（整个程序只调用一次）
    GdiplusStartupInput input;
    ULONG_PTR token;
    GdiplusStartup(&token, &input, NULL);

    int sWidth  = GetSystemMetrics(SM_CXSCREEN);
    int sHeight = GetSystemMetrics(SM_CYSCREEN);
    std::cout << "屏幕宽高: " << sWidth << "x" << sHeight << std::endl;

    //1.拿屏幕 DC 并创建兼容的内存 DC 和位图
    HDC hScreen = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, sWidth, sHeight);
    HGDIOBJ hOld = SelectObject(hMemDC, hBitmap);

    //2.把屏幕复制到位图（BitBlt 截屏，代替 CopyFromScreen）
    BitBlt(hMemDC, 0, 0, sWidth, sHeight, hScreen, 0, 0, SRCCOPY);

    //3.清理 GDI 资源
    SelectObject(hMemDC, hOld);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreen);

    //4-5.用 GDI+ 包装 HBITMAP 并保存
    //必须放在作用域块里，让 Bitmap 在 GdiplusShutdown 之前析构，否则退出时会崩溃
    {
        Bitmap bitmap(hBitmap, NULL);
        //MinGW 的 Save 第 3 个参数没有默认值，必须传 NULL
        CLSID pngClsid;
        if(GetEncoderClsid(L"image/png", &pngClsid) == -1){
            std::cout << "找不到 PNG 编码器" << std::endl;
            return -1;
        }
        Status st = bitmap.Save(L"test.png", &pngClsid, NULL);
        if(st == Ok){
            std::cout << "截图已保存到 test.png" << std::endl;
        } else {
            std::cout << "保存失败, Status=" << st << std::endl;
        }
    } //Bitmap 在这里析构

    //6.释放 HBITMAP，关闭 GDI+
    DeleteObject(hBitmap);
    GdiplusShutdown(token);
    system("pause");
    return 0;
}
