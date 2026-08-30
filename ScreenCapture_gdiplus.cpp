// test.txt 的 HandleScreen 的 GDI+ 版（函数形式，无 main）
//
// 用法：serve.hpp 里已有 Packet 结构、GetEncoderClsid、using namespace Gdiplus，
//       只需要把下面的 HandleScreen 函数覆盖到 serve.hpp 对应位置即可。
//       GDI+ 初始化（GdiplusStartup）放 main 开头只做一次，不要在函数里反复启停。
// 单独编译这个文件需要 -lgdiplus -lgdi32 -lole32。

#include <Windows.h>
#include <gdiplus.h>
#include <iostream>

using namespace Gdiplus;

//serve.hpp 里已定义这两个结构，这里仅为让文件能独立编译
#pragma pack(push, 1)
struct PacketHeader{ int magic; int cmd; int body_len; };
struct Packet{ PacketHeader header; char body[]; };
#pragma pack(pop)

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

//对应 test.txt 里的 HandleScreen，转成 GDI+ 内存流版本
int HandleScreen(Packet* pck){
    //GDI+ 初始化（CImage 免初始化，GDI+ 必须初始化才能用 Bitmap/Save）
    //注意：远控循环里反复调用时，应把这两行挪到 main 开头只执行一次
    GdiplusStartupInput input;
    ULONG_PTR token;
    GdiplusStartup(&token, &input, NULL);

    //2.拿到屏幕上下文（对应 CImage 版的 GetDC(NULL)）
    HDC hScreen = GetDC(NULL);
    //3.拿到屏幕像素位宽（GDI+ 的 CreateCompatibleBitmap 自动用屏幕位深，这里保留打印）
    int bitWidth = GetDeviceCaps(hScreen, BITSPIXEL);
    std::cout << "bitWidth:" << bitWidth << std::endl;
    //4.获取屏幕的宽高
    int sWidth  = GetSystemMetrics(SM_CXSCREEN);
    int sHeight = GetSystemMetrics(SM_CYSCREEN);

    //1.创建画布（对应 CImage image + image.Create(sWidth, sHeight, bitWidth)）
    HDC hMemDC = CreateCompatibleDC(hScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, sWidth, sHeight);
    HGDIOBJ hOld = SelectObject(hMemDC, hBitmap);

    //5.把屏幕数据复制到位图（对应 BitBlt(image.GetDC(), ...)，image.GetDC 换成 hMemDC）
    BitBlt(hMemDC, 0, 0, sWidth, sHeight, hScreen, 0, 0, SRCCOPY);

    //释放屏幕
    SelectObject(hMemDC, hOld);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreen);

    //6.用 GDI+ 包装 HBITMAP 并保存到内存流（对应 CImage 版 image.Save(pStream, ImageFormatPNG)）
    //放在作用域块里，让 Bitmap 在函数结束时先析构
    {
        Bitmap bitmap(hBitmap, NULL);

        CLSID pngClsid;
        if(GetEncoderClsid(L"image/png", &pngClsid) == -1){
            std::cout << "找不到 PNG 编码器" << std::endl;
            return -1;
        }

        //从堆上申请可变化的内存块（对应 HGLOBAL hMen = GlobalAlloc(GMEM_MOVEABLE, 0)）
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, 0);
        if(hMem == NULL){
            std::cout << "GlobalAlloc 失败" << std::endl;
            return -1;
        }
        //创建内存流（对应 CreateStreamOnHGlobal）
        IStream* pStream = NULL;
        HRESULT ret = CreateStreamOnHGlobal(hMem, TRUE, &pStream);
        if(ret != S_OK){
            std::cout << "CreateStreamOnHGlobal 失败" << std::endl;
            GlobalFree(hMem);
            return -1;
        }

        //保存 PNG 到内存流（MinGW 的 Save 第 3 个参数必须传 NULL）
        Gdiplus::Status st = bitmap.Save(pStream, &pngClsid, NULL);
        if(st != Gdiplus::Ok){
            std::cout << "保存失败, Status=" << st << std::endl;
            pStream->Release();
            GlobalFree(hMem);
            return -1;
        }

        //把流指针放到开头（第一个参数按值传）
        LARGE_INTEGER LG = {0};
        pStream->Seek(LG, STREAM_SEEK_SET, NULL);
        //获取缓冲区指针和长度（对应 GlobalLock / GlobalSize）
        char* pdata = (char*)GlobalLock(hMem);
        int len = GlobalSize(hMem);
        std::cout << "PNG 字节数: " << len << std::endl;

        //TODO: 把 pdata 的前 len 个字节塞进 Packet 发回客户端

        //清理内存流
        GlobalUnlock(hMem);
        pStream->Release();
        GlobalFree(hMem);
    } //Bitmap 在这里析构

    DeleteObject(hBitmap);
    GdiplusShutdown(token);
    return 0;
}
