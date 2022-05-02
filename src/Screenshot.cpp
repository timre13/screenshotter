#include "Screenshot.h"
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

void Screenshot::crop(int fromX, int fromY, int width, int height)
{
    assert(width > 0);
    assert(height > 0);
    assert(fromX + width < m_width);
    assert(fromY + height < m_height);

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

