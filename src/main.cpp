#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <iostream>
#include <string>
#include <ctime>
#include <cassert>
#include "Screenshot.h"

using uint = unsigned int;

/*
 * http://www.verycomputer.com/275_6ac8f0955e9280fa_1.htm
 */

bool g_isDisplayOpen = false;

//typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);

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

static constexpr const char* vertShaderSrc = "\
#version 130                                  \n\
                                              \n\
in vec2 inVert;                               \n\
in vec2 inTexCoord;                           \n\
                                              \n\
out vec2 texCoord;                            \n\
                                              \n\
void main()                                   \n\
{                                             \n\
    gl_Position = vec4(inVert.xy, 0.0f, 1.0); \n\
                                              \n\
    texCoord = inTexCoord;                    \n\
}                                             ";

static constexpr const char* fragShaderSrc = "\
#version 130                                  \n\
                                              \n\
in vec2 texCoord;                             \n\
                                              \n\
out vec4 outColor;                            \n\
                                              \n\
uniform sampler2D tex;                        \n\
                                              \n\
void main()                                   \n\
{                                             \n\
    outColor = texture(tex, texCoord).rgba;   \n\
}                                             ";

static uint createShader(bool isVert, const char* source)
{
    uint shaderId = glCreateShader(isVert ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    assert(shaderId);
    glShaderSource(shaderId, 1, &source, 0);

    glCompileShader(shaderId);
    int compStat{};
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &compStat);
    if (compStat != GL_TRUE)
    {
        int infoLogLen{};
        glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &infoLogLen);

        char* buff = new char[infoLogLen]{};
        glGetShaderInfoLog(shaderId, infoLogLen, nullptr, buff);
        std::cout << "ERR: Failed to compile "
            << (isVert ? "vertex" : "fragment")
            << " shader (id=" << shaderId << "): " << buff << '\n';
        delete[] buff;
        std::exit(1);
    }

    return shaderId;
}

static uint createShaderProg(const char* vertSource, const char* fragSource)
{
    uint vertShader = createShader(true, vertSource);
    uint fragShader = createShader(false, fragSource);
    uint prog = glCreateProgram();
    assert(prog);
    glAttachShader(prog, vertShader);
    glAttachShader(prog, fragShader);
    glLinkProgram(prog);

    int linkStat{};
    glGetProgramiv(prog, GL_LINK_STATUS, &linkStat);
    if (linkStat != GL_TRUE)
    {
        int infoLogLen{};
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &infoLogLen);
        char* buff = new char[infoLogLen];
        glGetProgramInfoLog(prog, infoLogLen, nullptr, buff);
        std::cout << "Failed to link shader program (id=" << prog << "): " << buff << '\n';
        delete[] buff;
        std::exit(1);
    }

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
    //int screeni = XDefaultScreen(disp);
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

    /*
    static constexpr int contextAttrs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 1,
        None
    };

    int fbConfCount{};
    GLXFBConfig* fbConf = glXChooseFBConfig(disp, screeni, (int*)visAttrs, &fbConfCount);

    glXCreateContextAttribsARBProc glXCreateContextAttribsARB =
        (glXCreateContextAttribsARBProc)glXGetProcAddressARB((const GLubyte *)"glXCreateContextAttribsARB");
    GLXContext glxCont = glXCreateContextAttribsARB(disp, fbConf, nullptr, GL_TRUE, (const int*)contextAttrs);
    */

    GLXContext glxCont = glXCreateContext(disp, visInf, nullptr, GL_TRUE);
    glXMakeCurrent(disp, glxWin, glxCont);

    glewExperimental = true;
    GLenum glewInitStat = glewInit();
    if (glewInitStat != GLEW_OK)
    {
        std::cerr << "Failed to initialize GLEW: " << glewGetErrorString(glewInitStat) << '\n';
        std::exit(1);
    }


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

    uint shader = createShaderProg(vertShaderSrc, fragShaderSrc);

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
