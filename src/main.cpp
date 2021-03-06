#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <iostream>
#include <string>
#include <ctime>
#include <cassert>
#include <cstdlib>
#include "Screenshot.h"
#include "utils.h"

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

#define IMG_VERT_ATTRIB_VERT_COORD 0
#define IMG_VERT_ATTRIB_TEX_COORD 1
#define SEL_VERT_ATTRIB_VERT_COORD 0
#define SEL_VERT_ATTRIB_REL_COORD 1

static constexpr const char* imgVertShaderSrc = "\
#version 130                                  \n\
                                              \n\
in vec3 inVert;                               \n\
in vec2 inTexCoord;                           \n\
                                              \n\
out vec2 texCoord;                            \n\
                                              \n\
void main()                                   \n\
{                                             \n\
    gl_Position = vec4(inVert.xyz, 1.0);      \n\
                                              \n\
    texCoord = inTexCoord;                    \n\
}                                             ";

static constexpr const char* imgFragShaderSrc = "\
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

//------------------------------------------------------------

static constexpr const char* selectionVertShaderSrc = "\
#version 130                                  \n\
                                              \n\
in vec3 inVert;                               \n\
in vec2 inRelCoord;                           \n\
                                              \n\
out vec2 relCoord;                            \n\
                                              \n\
void main()                                   \n\
{                                             \n\
    gl_Position = vec4(inVert.xyz, 1.0);      \n\
    relCoord = inRelCoord;                    \n\
}                                             ";

static constexpr const char* selectionFragShaderSrc = "\
#version 130                                  \n\
                                              \n\
in vec2 relCoord;                             \n\
                                              \n\
out vec4 outColor;                            \n\
                                              \n\
uniform vec2 realSize;                        \n\
                                              \n\
#define BORD_W 5                              \n\
                                              \n\
void main()                                   \n\
{                                             \n\
    if (relCoord.x*realSize.x <= BORD_W       \n\
     || relCoord.x >= 1.0-BORD_W/realSize.x   \n\
     || relCoord.y*realSize.y <= BORD_W       \n\
     || relCoord.y >= 1.0-BORD_W/realSize.y   \n\
    )                                         \n\
        outColor = vec4(0.1, 0.4, 0.6, 0.5);  \n\
    else                                      \n\
        outColor = vec4(0.9, 0.6, 0.4, 0.5);  \n\
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

    return prog;
}

struct WinGeometry
{
    int x{};
    int y{};
    int w{};
    int h{};
};

static Window getToplevelWin(Display* disp, Window win)
{
    while (true)
    {
        Window rootWin;
        Window parentWin;
        Window* children;
        uint childCount;
        if (XQueryTree(disp, win, &rootWin, &parentWin, &children, &childCount) == 0)
        {
            std::cerr << "Failed to get top level window\n";
            notifShow("Screenshot Error", "Failed to get top level window");
            throw std::runtime_error{"Failed to get top level window"};
        }

        if (children)
            XFree(children);

        // If this is the root or this is a top-level window
        if (win == rootWin || parentWin == rootWin)
            return win;
        else
            win = parentWin; // Go to the next parent
    }
}

WinGeometry getFocusedWinGeom(Display* disp)
{
    WinGeometry out;

    Window win;
    int focusState;
    XGetInputFocus(disp, &win, &focusState);
    try
    {
        win = getToplevelWin(disp, win);
    }
    catch (...)
    {
        return {};
    }

    XWindowAttributes attrs;
    XGetWindowAttributes(disp, win, &attrs);
    assert(attrs.width > 1 && attrs.height > 1);
    out.x = attrs.x;
    out.y = attrs.y;
    out.w = attrs.width;
    out.h = attrs.height;

    return out;
}

WinGeometry getCurrentMonitorGeom(Display* disp)
{
    WinGeometry out;

    const Window rootWin = XDefaultRootWindow(disp);
    int monCount;
    XRRMonitorInfo* monInfoArr = XRRGetMonitors(disp, rootWin, false, &monCount);
    std::cout << "There are " << monCount << " monitors\n";
    for (int i{}; i < monCount; ++i)
    {
        char* name = XGetAtomName(disp, monInfoArr[i].name);
        std::cout << '\t' << name << ": (w=" << monInfoArr[i].width << ", h=" << monInfoArr[i].height << ") "
            "@ (" << monInfoArr[i].x << ", " << monInfoArr[i].y << ")\n";
        XFree(name);
    }

    int cursX, cursY;
    {
        Window rootRet;
        Window childWin;
        int winX, winY;
        uint btnMask;
        XQueryPointer(disp, rootWin, &rootRet, &childWin, &cursX, &cursY, &winX, &winY, &btnMask);
    }
    std::cout << "Pointer is at " << cursX << ", " << cursY << '\n';

    // Index of monitor where the cursor is
    int cursMonI = -1;
    for (int i{}; i < monCount; ++i)
    {
        const int mx1 = monInfoArr[i].x;
        const int my1 = monInfoArr[i].y;
        const int mx2 = mx1 + monInfoArr[i].width;
        const int my2 = my1 + monInfoArr[i].height;

        if (cursX >= mx1 && cursX <= mx2 && cursY >= my1 && cursY <= my2)
        {
            cursMonI = i;
            break;
        }
    }
    if (cursMonI == -1)
    {
        std::cerr << "Failed to get focused monitor\n";
        notifShow("Screenshot Error", "Failed to get focused monitor");
        throw std::runtime_error{"Failed to get focused monitor"};
    }

    std::cout << "Pointer is on monitor " << cursMonI << '\n';

    out.x = monInfoArr[cursMonI].x;
    out.y = monInfoArr[cursMonI].y;
    out.w = monInfoArr[cursMonI].width;
    out.h = monInfoArr[cursMonI].height;

    XRRFreeMonitors(monInfoArr);
    return out;
}

enum class ScreenshotType
{
    CroppedOrFull,
    FocusedWindow,
    CurrentScreen,
};

int main()
{
    notifInit("Screenshot");

    XSetErrorHandler(&xErrHandler);

    Display* disp = XOpenDisplay(nullptr);
    assert(disp);
    g_isDisplayOpen = true;

    Screenshot sshot{disp};

    Screen* screen = XDefaultScreenOfDisplay(disp);
    //int screeni = XDefaultScreen(disp);
    Window rootWin = XRootWindowOfScreen(screen);

    XWindowAttributes attrs{};
    Status rets = XGetWindowAttributes(disp, rootWin, &attrs);
    assert(rets);
    std::cout << "Root window size is: " << attrs.width << "x" << attrs.height << '\n';

    WinGeometry focusedWinGeom = getFocusedWinGeom(disp);
    std::cout << "Focused window geometry: (x=" << focusedWinGeom.x << ", y="
        << focusedWinGeom.y << ") (w=" << focusedWinGeom.w << ", h=" << focusedWinGeom.h << ")\n";

    XSetWindowAttributes winAttrs{};
    winAttrs.border_pixel = 0;
    winAttrs.event_mask = ButtonPressMask|ButtonReleaseMask|KeyPressMask|KeyReleaseMask|PointerMotionMask|ExposureMask|ClientMessage;
    winAttrs.override_redirect = true;
    winAttrs.save_under = true;
    static constexpr int visAttrs[] = {
        GLX_RGBA,
        GLX_DEPTH_SIZE, 24,
        GLX_DOUBLEBUFFER,
        None

    };
    XVisualInfo* visInf = glXChooseVisual(disp, 0, (int*)visAttrs);
    assert(visInf);
    winAttrs.colormap = XCreateColormap(disp, rootWin, visInf->visual, AllocNone);

    Cursor curs = XCreateFontCursor(disp, XC_crosshair);

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
    XGrabKeyboard(disp, glxWin, false, GrabModeAsync, GrabModeAsync, CurrentTime);
    XGrabPointer(disp, glxWin, false, ButtonMotionMask|ButtonPressMask|ButtonReleaseMask, GrabModeAsync, GrabModeAsync, glxWin, curs, CurrentTime);

    XDefineCursor(disp, glxWin, curs);

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

    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
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

    uint imgShader = createShaderProg(imgVertShaderSrc, imgFragShaderSrc);

    const float imgVertCoords[] = {
        -1, -1, 0, /**/ 0, 1, // 0 - Top left
        -1,  1, 0, /**/ 0, 0, // 1 - Bottom left
         1, -1, 0, /**/ 1, 1, // 2 - Top right
         1,  1, 0, /**/ 1, 0, // 3 - Bottom right
    };
    const int vertIndices[] = {
        1, 0, 2, 1, 3, 2
    };
    uint imgVao{};
    glGenVertexArrays(1, &imgVao);
    glBindVertexArray(imgVao);

    uint imgVbo{};
    glGenBuffers(1, &imgVbo);
    glBindBuffer(GL_ARRAY_BUFFER, imgVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(imgVertCoords), imgVertCoords, GL_STATIC_DRAW);

    uint imgEbo{};
    glGenBuffers(1, &imgEbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, imgEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(vertIndices), vertIndices, GL_STATIC_DRAW);

    glVertexAttribPointer(IMG_VERT_ATTRIB_VERT_COORD, 3, GL_FLOAT, false, sizeof(float)*5, (void*)0);
    glEnableVertexAttribArray(IMG_VERT_ATTRIB_VERT_COORD);
    glVertexAttribPointer(IMG_VERT_ATTRIB_TEX_COORD, 2, GL_FLOAT, false, sizeof(float)*5, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(IMG_VERT_ATTRIB_TEX_COORD);

    //------------------------------------------------------------

    uint selectionShader = createShaderProg(selectionVertShaderSrc, selectionFragShaderSrc);

    float selVertCoords[] = {
        0, 0, 0, /**/ 0, 1, // 0 - Top left
        0, 0, 0, /**/ 0, 0, // 1 - Bottom left
        0, 0, 0, /**/ 1, 1, // 2 - Top right
        0, 0, 0, /**/ 1, 0, // 3 - Bottom right
    };

    uint selectionVao{};
    glGenVertexArrays(1, &selectionVao);
    glBindVertexArray(selectionVao);

    uint selectionVbo{};
    glGenBuffers(1, &selectionVbo);
    glBindBuffer(GL_ARRAY_BUFFER, selectionVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(selVertCoords), nullptr, GL_DYNAMIC_DRAW);

    uint selectionEbo{};
    glGenBuffers(1, &selectionEbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, selectionEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(vertIndices), vertIndices, GL_STATIC_DRAW);

    glVertexAttribPointer(SEL_VERT_ATTRIB_VERT_COORD, 3, GL_FLOAT, false, sizeof(float)*5, (void*)0);
    glEnableVertexAttribArray(SEL_VERT_ATTRIB_VERT_COORD);
    glVertexAttribPointer(SEL_VERT_ATTRIB_REL_COORD, 2, GL_FLOAT, false, sizeof(float)*5, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(SEL_VERT_ATTRIB_REL_COORD);

    //------------------------------------------------------------

    uint tex{};
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, sshot.getWidth(), sshot.getHeight(), 0, GL_BGRA, GL_UNSIGNED_BYTE, sshot.getDataPtr());
    glGenerateMipmap(GL_TEXTURE_2D);

    glUniform1i(glGetUniformLocation(imgShader, "tex"), 0);

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    ScreenshotType sshotType = ScreenshotType::CroppedOrFull;

    bool isDragging = false;
    int mouseX{};
    int mouseY{};
    int selStartX{};
    int selStartY{};
    bool done = false;
    bool cancelled = false;
    while (!done)
    {
        XEvent event{};
        while (XPending(disp)) // While there are events in the queue
        {
            XNextEvent(disp, &event);
            switch (event.type)
            {
                case KeyRelease:
                {
                    const auto key = XLookupKeysym(&event.xkey, 0);
                    if (key == XK_q || key == XK_Escape)
                    {
                        done = true;
                        cancelled = true;
                    }
                    else if (key == XK_Return)
                    {
                        done = true;
                        cancelled = false;
                    }
                    else if (key == XK_w)
                    {
                        done = true;
                        cancelled = false;
                        sshotType = ScreenshotType::FocusedWindow;
                    }
                    else if (key == XK_s)
                    {
                        done = true;
                        cancelled = false;
                        sshotType = ScreenshotType::CurrentScreen;
                    }
                    break;
                }

                case ClientMessage:
                    // If the message is "WM_DELETE_WINDOW"
                    if ((Atom)(event.xclient.data.l[0]) == wmDeleteMessage)
                    {
                        done = true;
                        cancelled = true;
                    }
                    break;

                case MotionNotify:
                {
                    std::cout << "Moved pointer to: " << event.xmotion.x << ", " << event.xmotion.y << '\n';
                    mouseX = event.xmotion.x;
                    mouseY = event.xmotion.y;
                    break;
                }

                case ButtonPress:
                {
                    std::cout << "Pressed mouse button: " << event.xbutton.button << '\n';
                    if (event.xbutton.button == 1)
                    {
                        isDragging = true;
                        mouseX = event.xmotion.x;
                        mouseY = event.xmotion.y;
                        selStartX = mouseX;
                        selStartY = mouseY;
                    }
                    break;
                }

                case ButtonRelease:
                {
                    std::cout << "Released mouse button: " << event.xbutton.button << '\n';
                    if (event.xbutton.button == 1)
                        isDragging = false;
                    break;
                }
            }
        }

        glClearColor(0.8f, 0.8f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(imgShader);
        glBindVertexArray(imgVao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);


        glUseProgram(selectionShader);
        glBindVertexArray(selectionVao);
        if (isDragging)
        {
            glUniform2f(glGetUniformLocation(selectionShader, "realSize"),
                    std::abs(selStartX-mouseX), std::abs(selStartY-mouseY));

            const float x1 = float(selStartX)/sshot.getWidth()*2-1.0f;
            const float y1 = float(sshot.getHeight()-selStartY)/sshot.getHeight()*2-1.0f;
            const float x2 = float(mouseX)/sshot.getWidth()*2-1.0f;
            const float y2 = float(sshot.getHeight()-mouseY)/sshot.getHeight()*2-1.0f;
            selVertCoords[0]  = x1; selVertCoords[1]  = y1;
            selVertCoords[5]  = x1; selVertCoords[6]  = y2;
            selVertCoords[10] = x2; selVertCoords[11] = y1;
            selVertCoords[15] = x2; selVertCoords[16] = y2;

            glBindBuffer(GL_ARRAY_BUFFER, selectionVbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(selVertCoords), selVertCoords);
        }
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

        glXSwapBuffers(disp, glxWin);
    }

    if (!cancelled)
    {
        bool didSelectionCropping = false;
        if (sshotType == ScreenshotType::CroppedOrFull)
        {
            const int xPos = std::min(selStartX, mouseX);
            const int yPos = std::min(selStartY, mouseY);
            const int width = std::abs(selStartX-mouseX);
            const int height = std::abs(selStartY-mouseY);
            if (width > 0 && height > 0)
            {
                std::cout << "Cropping: position: (" << xPos << ", " << yPos << "), size: " << width << 'x' << height << '\n';
                std::cout.flush();
                sshot.crop(xPos, yPos, width, height);
                didSelectionCropping = true;
            }
            else
            {
                std::cout << "Not cropping\n";
                std::cout.flush();
            }
        }
        else if (sshotType == ScreenshotType::FocusedWindow)
        {
            std::cout << "Cropping to focused window geometry\n";
            sshot.crop(focusedWinGeom.x, focusedWinGeom.y, focusedWinGeom.w, focusedWinGeom.h);
        }
        else if (sshotType == ScreenshotType::CurrentScreen)
        {
            std::cout << "Cropping to cursor monitor geometry\n";
            const WinGeometry geom = getCurrentMonitorGeom(disp);
            sshot.crop(geom.x, geom.y, geom.w, geom.h);
        }

        { // Write to file
            std::string filename;
            if (const char* homeDir = getenv("HOME"))
            {
                filename = std::string(homeDir)+"/Pictures/";
            }
            else
            {
                std::cerr << "WARN: Failed to get $HOME\n";
            }
            filename += genFilenamePref()+".png";

            sshot.writeToPNGFile(filename);
            std::cout << "Saved screenshot to \""+filename+"\"\n";

            if (sshotType == ScreenshotType::FocusedWindow)
                notifShow("Created screenshot of focused window", "Saved screenshot to \""+filename+"\"");
            else if (sshotType == ScreenshotType::CurrentScreen)
                notifShow("Created screenshot of current screen", "Saved screenshot to \""+filename+"\"");
            else if (sshotType == ScreenshotType::CroppedOrFull && didSelectionCropping)
                notifShow("Created screenshot of selected area",  "Saved screenshot to \""+filename+"\"");
            else if (sshotType == ScreenshotType::CroppedOrFull)
                notifShow("Created screenshot of all screens",    "Saved screenshot to \""+filename+"\"");
        }

        sshot.copyToClipboard();
        std::cout << "Copied screenshot to clipboard\n";
    }
    else
    {
        std::cout << "Cancelled\n";
    }

    glDeleteBuffers(1, &imgVbo);
    glDeleteBuffers(1, &imgEbo);
    glDeleteVertexArrays(1, &imgVao);
    glDeleteProgram(imgShader);
    glDeleteBuffers(1, &selectionVbo);
    glDeleteBuffers(1, &selectionEbo);
    glDeleteVertexArrays(1, &selectionVao);
    glDeleteProgram(selectionShader);
    sshot.destroy();
    XUngrabKeyboard(disp, CurrentTime);
    XUngrabPointer(disp, CurrentTime);
    XCloseDisplay(disp);
    XFreeCursor(disp, curs);
    g_isDisplayOpen = false;
    notifUninit();
    return 0;
}
