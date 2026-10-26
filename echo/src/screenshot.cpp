// Echo UI 库 - 截图实现（BMP，24 位，无压缩）。
//
// 读的是 GL_FRONT：调用点通常在 endFrame() 之后（缓冲已经交换），
// 此时前台缓冲里才是"用户看到的这一帧"。
#include "echo/screenshot.h"

#include <cstdint>
#include <fstream>
#include <vector>

#include <GLFW/glfw3.h>

namespace echo {

namespace {

void writeU16(std::ofstream& out, std::uint32_t value)
{
    const char bytes[2] = {static_cast<char>(value & 0xFF), static_cast<char>((value >> 8) & 0xFF)};
    out.write(bytes, 2);
}

void writeU32(std::ofstream& out, std::uint32_t value)
{
    const char bytes[4] = {static_cast<char>(value & 0xFF), static_cast<char>((value >> 8) & 0xFF),
                           static_cast<char>((value >> 16) & 0xFF), static_cast<char>((value >> 24) & 0xFF)};
    out.write(bytes, 4);
}

bool saveBmp(const std::string& path, const std::vector<unsigned char>& rgba, int width, int height)
{
    // 每行补齐到 4 字节（BMP 的要求）。
    const int rowSize = (width * 3 + 3) & ~3;
    const int imageSize = rowSize * height;

    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;

    out.write("BM", 2);
    writeU32(out, 54 + imageSize); // 文件大小
    writeU32(out, 0);              // 保留
    writeU32(out, 54);             // 像素数据偏移
    writeU32(out, 40);             // BITMAPINFOHEADER 大小
    writeU32(out, static_cast<std::uint32_t>(width));
    writeU32(out, static_cast<std::uint32_t>(height)); // 正数 = 自下而上，与 glReadPixels 一致
    writeU16(out, 1);                                  // 平面数
    writeU16(out, 24);                                 // 位深
    writeU32(out, 0);                                  // 不压缩
    writeU32(out, static_cast<std::uint32_t>(imageSize));
    writeU32(out, 2835); // 96 DPI
    writeU32(out, 2835);
    writeU32(out, 0);
    writeU32(out, 0);

    std::vector<unsigned char> row(static_cast<size_t>(rowSize), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t index = (static_cast<size_t>(y) * width + x) * 4;
            row[x * 3 + 0] = rgba[index + 2]; // B
            row[x * 3 + 1] = rgba[index + 1]; // G
            row[x * 3 + 2] = rgba[index + 0]; // R
        }
        out.write(reinterpret_cast<const char*>(row.data()), rowSize);
    }
    return out.good();
}

} // namespace

bool captureFramebufferBmp(const std::string& path)
{
    GLFWwindow* window = glfwGetCurrentContext();
    if (window == nullptr)
        return false;

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0)
        return false;

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_FRONT); // 已交换，前台才是画好的那一帧
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    return saveBmp(path, pixels, width, height);
}

} // namespace echo
