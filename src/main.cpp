#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <iostream>
#include <string>
#include <ctime>
#include <cassert>
#include "Screenshot.h"

/*
 * http://www.verycomputer.com/275_6ac8f0955e9280fa_1.htm
 */

bool g_isDisplayOpen = false;

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
