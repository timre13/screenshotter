#include "Screenshot.h"
#include <sys/shm.h>
#include <sys/ipc.h>
#include <fstream>
#include <errno.h>
#include <cstring>
#include <cassert>

extern bool g_isDisplayOpen;

Screenshot::Screenshot(Display* disp)
    : m_disp{disp}, m_shmInfo{new XShmSegmentInfo{}}
{
    m_screen = XDefaultScreenOfDisplay(disp);
    const int screeni = XDefaultScreen(disp);

    const Window win = XRootWindowOfScreen(m_screen);

    // Get root window info
    XWindowAttributes attrs{};
    Status rets = XGetWindowAttributes(disp, win, &attrs);
    assert(rets);

    m_img = XShmCreateImage(
            disp, // Display
            DefaultVisual(disp, screeni), // Visual (display format)
            DefaultDepthOfScreen(m_screen), // Depth
            ZPixmap, // Format
            nullptr, // Data
            m_shmInfo, // shminfo
            attrs.width, // width
            attrs.height // height
    );

    // Create a shared memory buffer
    m_shmInfo->shmid = shmget(IPC_PRIVATE, m_img->bytes_per_line*m_img->height, IPC_CREAT|0777);
    m_img->data = (char*)shmat(m_shmInfo->shmid, nullptr, 0);
    m_shmInfo->shmaddr = m_img->data;
    m_shmInfo->readOnly = false;

    // Bind the buffer
    Bool retb = XShmAttach(disp, m_shmInfo);
    assert(retb);

    // Copy the image from the root window to the image buffer
    retb = XShmGetImage(disp, win, m_img, 0, 0, AllPlanes);
    assert(retb);
}

void Screenshot::writeToPPMFile(const std::string& filename) const
{
    std::fstream file;
    file.open(filename, std::ios_base::out|std::ios_base::binary);
    if (!file.is_open())
    {
        throw std::runtime_error{"Failed to write to file: \""+filename+"\": "+std::strerror(errno)};
    }

    // Magic bytes
    file.write("P6\n", 3);

    const std::string widthStr = std::to_string(getWidth()) + "\n";
    file.write(widthStr.c_str(), widthStr.length());

    const std::string heightStr = std::to_string(getHeight()) + "\n";
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

Screenshot::~Screenshot()
{
    assert(g_isDisplayOpen);
    // Unbind the shared buffer
    XShmDetach(m_disp, m_shmInfo);
    shmdt(m_shmInfo->shmaddr);
    // Delete the shared buffer
    shmctl(m_shmInfo->shmid, IPC_RMID, 0);
}

