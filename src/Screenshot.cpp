#include "Screenshot.h"
#include <iostream>
#include <libpng/png.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <fstream>
#include <errno.h>
#include <cstring>
#include <cassert>
#include <memory.h>

extern bool g_isDisplayOpen;

Screenshot::Screenshot(Display* disp)
{
    Screen* screen = XDefaultScreenOfDisplay(disp);
    const int screeni = XDefaultScreen(disp);

    const Window win = XRootWindowOfScreen(screen);

    // Get root window info
    XWindowAttributes attrs{};
    Status rets = XGetWindowAttributes(disp, win, &attrs);
    assert(rets);

    XShmSegmentInfo* shmInfo = new XShmSegmentInfo{};

    XImage* img = XShmCreateImage(
            disp, // Display
            DefaultVisual(disp, screeni), // Visual (display format)
            DefaultDepthOfScreen(screen), // Depth
            ZPixmap, // Format
            nullptr, // Data
            shmInfo, // shminfo
            attrs.width, // width
            attrs.height // height
    );

    // Create a shared memory buffer
    shmInfo->shmid = shmget(IPC_PRIVATE, img->bytes_per_line*img->height, IPC_CREAT|0777);
    img->data = (char*)shmat(shmInfo->shmid, nullptr, 0);
    shmInfo->shmaddr = img->data;
    shmInfo->readOnly = false;

    // Bind the buffer
    Bool retb = XShmAttach(disp, shmInfo);
    assert(retb);

    // Copy the image from the root window to the image buffer
    retb = XShmGetImage(disp, win, img, 0, 0, AllPlanes);
    assert(retb);

    m_width = img->width;
    m_height = img->height;
    m_bytesPerLine = img->bytes_per_line;
    m_data = new uint8_t[m_bytesPerLine*m_height];
    std::memcpy(m_data, img->data, m_bytesPerLine*m_height);

    for (int i{}; i < m_width*m_height; ++i)
    {
        // Set alpha to 255
        m_data[i*BYTES_PER_PIXEL+3] = 255;
    }

    // Unbind the shared buffer
    XShmDetach(disp, shmInfo);
    shmdt(shmInfo->shmaddr);
    // Delete the shared buffer
    shmctl(shmInfo->shmid, IPC_RMID, 0);
    delete shmInfo;
}

void Screenshot::writeToPPMFile(const std::string& filename) const
{
    assert(m_data);

    std::fstream file;
    file.open(filename, std::ios_base::out|std::ios_base::binary);
    if (!file.is_open())
    {
        throw std::runtime_error{"Failed to write to file: \""+filename+"\": "+std::strerror(errno)};
    }

    // Magic bytes
    file.write("P6\n", 3);

    const std::string widthStr = std::to_string(m_width) + "\n";
    file.write(widthStr.c_str(), widthStr.length());

    const std::string heightStr = std::to_string(m_height) + "\n";
    file.write(heightStr.c_str(), heightStr.length());

    // Max component value
    file.write("255\n", 4);

    for (int i{}; i < getPixelCount(); ++i)
    {
        const Screenshot::Pixel pxl = getPixel(i);
        file.put(pxl.r);
        file.put(pxl.g);
        file.put(pxl.b);
    }
    file.close();
}

void Screenshot::writeToPNGFile(const std::string& filename) const
{
    // --- Init ---

    FILE* fp = fopen(filename.c_str(), "wb");
    assert(fp);

    png_structp pngPtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    assert(pngPtr);

    png_infop infoPtr = png_create_info_struct(pngPtr);
    assert(infoPtr);

    int ret = setjmp(png_jmpbuf(pngPtr));
    assert(ret == 0);

    png_init_io(pngPtr, fp);

    // --- Write header ---

    ret = setjmp(png_jmpbuf(pngPtr));
    assert(ret == 0);

    png_set_IHDR(pngPtr, infoPtr, m_width, m_height, 8,
            PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
            PNG_FILTER_TYPE_BASE);

    png_write_info(pngPtr, infoPtr);

    // --- Write data ---

    ret = setjmp(png_jmpbuf(pngPtr));
    assert(ret == 0);
    uint8_t* row = new uint8_t[m_width*3];
    for (int y{}; y < m_height; ++y)
    {
        // Copy data to temporary buffer without alpha and fix byte order
        for (int x{}; x < m_width; ++x)
        {
            row[x*3+0] = m_data[y*m_bytesPerLine+x*4+2];
            row[x*3+1] = m_data[y*m_bytesPerLine+x*4+1];
            row[x*3+2] = m_data[y*m_bytesPerLine+x*4+0];
        }
        png_write_row(pngPtr, row);
    }
    delete[] row;

    // --- End write ---

    ret = setjmp(png_jmpbuf(pngPtr));
    assert(ret == 0);
    png_write_end(pngPtr, infoPtr);

    fclose(fp);
}

void Screenshot::copyToClipboard() const
{
    assert(m_data);
    // TODO: Find a better way

    // Write to a temporary file and use xclip to copy the file data to clipboard
    writeToPNGFile("/tmp/sshot_img.png");
    std::system("xclip -selection clipboard -t image/png /tmp/sshot_img.png");
}

void Screenshot::crop(int fromX, int fromY, int width, int height)
{
    assert(width > 0);
    assert(height > 0);
    assert(fromX + width <= m_width);
    assert(fromY + height <= m_height);

    const int bytesPerLine = width*BYTES_PER_PIXEL;
    uint8_t* buff = new uint8_t[height*bytesPerLine]{};
    std::memset(buff, 100, height*bytesPerLine);

    for (int yoffs{}; yoffs < height; ++yoffs)
    {
        std::memcpy(
                buff+yoffs*bytesPerLine,
                m_data+(fromY+yoffs)*m_bytesPerLine+fromX*BYTES_PER_PIXEL,
                width*BYTES_PER_PIXEL);
    }

    delete[] m_data;
    m_data = buff;
    m_width = width;
    m_height = height;
    m_bytesPerLine = bytesPerLine;
}

void Screenshot::destroy()
{
    delete[] m_data;
    m_data = nullptr;
    m_width = 0;
    m_height = 0;
    m_bytesPerLine = 0;
}

Screenshot::~Screenshot()
{
    if (m_data)
        destroy();
}

