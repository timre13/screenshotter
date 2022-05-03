#pragma once

#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <cstdint>
#include <string>

#define BYTES_PER_PIXEL 4

class Screenshot
{
private:
    uint8_t* m_data{};
    int m_width{};
    int m_height{};
    int m_bytesPerLine{};

public:
    Screenshot(Display* disp);

    inline int getWidth() const { return m_width; }
    inline int getHeight() const { return m_height; }
    inline int getPixelCount() const { return m_width*m_height; }

    struct Pixel
    {
        uint8_t r{};
        uint8_t g{};
        uint8_t b{};
    };

    inline Pixel getPixel(int index) const
    {
        return {
            (uint8_t)m_data[index * BYTES_PER_PIXEL + 2], // R
            (uint8_t)m_data[index * BYTES_PER_PIXEL + 1], // G
            (uint8_t)m_data[index * BYTES_PER_PIXEL + 0], // B
        };
    }

    inline const uint8_t* getDataPtr() const
    {
        return m_data;
    }

    void crop(int fromX, int fromY, int width, int height);

    void writeToPPMFile(const std::string& filename) const;
    void copyToClipboard() const;

    void destroy();
    ~Screenshot();
};

