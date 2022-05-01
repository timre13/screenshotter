#pragma once

#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <cstdint>
#include <string>

class Screenshot
{
private:
    Display*            m_disp{};
    Screen*             m_screen{};
    // Handle to the shared memory buffer
    XShmSegmentInfo*    m_shmInfo{};
    // The screenshot itself
    XImage*             m_img{};

public:
    Screenshot(Display* disp);

    inline int getWidth() const { return m_img->width; }
    inline int getHeight() const { return m_img->height; }
    inline int getPixelCount() const { return m_img->width*m_img->height; }

    struct Pixel
    {
        uint8_t r{};
        uint8_t g{};
        uint8_t b{};
    };

    inline Pixel getPixel(int index) const
    {
        return {
            (uint8_t)m_img->data[index * 4 + 2], // R
            (uint8_t)m_img->data[index * 4 + 1], // G
            (uint8_t)m_img->data[index * 4 + 0], // B
        };
    }

    inline const unsigned char* getDataPtr() const
    {
        return (unsigned char*)m_img->data;
    }

    void writeToPPMFile(const std::string& filename) const;

    void destroy();
    ~Screenshot();
};

