#include <cassert>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <fstream>
#include <iostream>
#include <ctime>
#include <cstdint>
#include <errno.h>
#include <cstring>

/*
 * http://www.verycomputer.com/275_6ac8f0955e9280fa_1.htm
 */

bool g_isDisplayOpen = false;

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
    Screenshot(Display* disp)
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

    inline int getWidth() const { return m_img->width; }
    inline int getHeight() const { return m_img->height; }
    inline int getPixelCount() const { return m_img->width*m_img->height; }

    struct Pixel
    {
        uint8_t r{};
        uint8_t g{};
        uint8_t b{};
    };

    inline Pixel getPixel(int index)
    {
        return {
            (uint8_t)m_img->data[index * 4 + 2], // R
            (uint8_t)m_img->data[index * 4 + 1], // G
            (uint8_t)m_img->data[index * 4 + 0], // B
        };
    }

    inline void writeToPPMFile(const std::string& filename)
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

    ~Screenshot()
    {
        assert(g_isDisplayOpen);
        // Unbind the shared buffer
        XShmDetach(m_disp, m_shmInfo);
        shmdt(m_shmInfo->shmaddr);
        // Delete the shared buffer
        shmctl(m_shmInfo->shmid, IPC_RMID, 0);
    }
};

static std::string genFilenamePref()
{
    time_t time = std::time(nullptr);
    tm* localTM = std::localtime(&time);
    char buff[sizeof("0000-00-00-000000")]{};
    std::strftime(buff, sizeof(buff), "%F-%H%M%S", localTM);
    return buff;
}

static int xErrHandler(Display* disp, XErrorEvent* event)
{
    char buff[1024]{};
    XGetErrorText(disp, event->error_code, buff, sizeof(buff));

    std::cerr << "X Error: " << buff << '\n';
    std::cerr.flush();
    std::exit(13);
    return 0;
}

int main()
{
    XSetErrorHandler(&xErrHandler);

    Display* disp = XOpenDisplay(nullptr);
    assert(disp);
    g_isDisplayOpen = true;

    {
        Screenshot sshot{disp};
        const std::string filename = genFilenamePref()+".ppm";
        sshot.writeToPPMFile(filename);
        std::cout << "Saved screenshot to \""+filename+"\"\n";
    }

    Screen* screen = XDefaultScreenOfDisplay(disp);
    Window rootWin = XRootWindowOfScreen(screen);

    XWindowAttributes attrs{};
    Status rets = XGetWindowAttributes(disp, rootWin, &attrs);
    assert(rets);

    XSetWindowAttributes winAttrs{};
    winAttrs.border_pixel = 0;
    winAttrs.event_mask = StructureNotifyMask;
    static constexpr int visAttrs[] = {
        GLX_RGBA,
        GLX_DEPTH_SIZE, 24,
        GLX_DOUBLEBUFFER,
        None

    };
    XVisualInfo* visInf = glXChooseVisual(disp, 0, (int*)visAttrs);
    assert(visInf);
    winAttrs.colormap = XCreateColormap(disp, rootWin, visInf->visual, AllocNone);

    Window glxWin = XCreateWindow(
            disp,
            rootWin,
            0, 0,
            attrs.width, attrs.height,
            0,
            visInf->depth,
            InputOutput,
            visInf->visual,
            CWColormap|CWEventMask|CWOverrideRedirect|CWSaveUnder,
            &winAttrs
    );
    XMapWindow(disp, glxWin);

    GLXContext glxCont = glXCreateContext(disp, visInf, nullptr, GL_TRUE);
    glXMakeCurrent(disp, glxWin, glxCont);
    glViewport(0, 0, attrs.width, attrs.height);

    glClearColor(0.8f, 0.4f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glXSwapBuffers(disp, glxWin);

    // Set window name and class
    XClassHint* hints = XAllocClassHint();
    char wmName[] = "Screenshot";
    char wmClass[] = "Shot";
    hints->res_name = wmName;
    hints->res_class = wmClass;
    XStoreName(disp, glxWin, wmName);
    XSetClassHint(disp, glxWin, hints);

    // Tell X to send a `ClientMessage` event on window close
    Atom wmDeleteMessage = XInternAtom(disp, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(disp, glxWin, &wmDeleteMessage, 1);

    bool done = false;
    while (!done)
    {
        XEvent event{};
        if (XPending(disp)) // If there are events in the queue
        {
            XNextEvent(disp, &event);
            switch (event.type)
            {
                case KeyRelease:
                    done = true;
                    break;

                case ClientMessage:
                    // If the message is "WM_DELETE_WINDOW"
                    if ((Atom)(event.xclient.data.l[0]) == wmDeleteMessage)
                        done = true;
                    break;
            }
        }

        glClearColor(0.8f, 0.4f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);


        glXSwapBuffers(disp, glxWin);
    }

    XCloseDisplay(disp);
    g_isDisplayOpen = false;
    return 0;
}
