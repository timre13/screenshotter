#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
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

#define VERT_ATTRIB_VERT_COORD 0
#define VERT_ATTRIB_TEX_COORD 1

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
                                              \n\
void main()                                   \n\
{                                             \n\
    gl_Position = vec4(inVert.xyz, 1.0);      \n\
}                                             ";

static constexpr const char* selectionFragShaderSrc = "\
#version 130                                  \n\
                                              \n\
out vec4 outColor;                            \n\
                                              \n\
void main()                                   \n\
{                                             \n\
    outColor = vec4(0.9, 0.6, 0.4, 0.5);      \n\
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

int main()
{
    XSetErrorHandler(&xErrHandler);

    Display* disp = XOpenDisplay(nullptr);
    assert(disp);
    g_isDisplayOpen = true;

    Screenshot sshot{disp};
    const std::string filename = genFilenamePref()+".ppm";
    sshot.writeToPPMFile(filename);
    std::cout << "Saved screenshot to \""+filename+"\"\n";

    Screen* screen = XDefaultScreenOfDisplay(disp);
    //int screeni = XDefaultScreen(disp);
    Window rootWin = XRootWindowOfScreen(screen);

    XWindowAttributes attrs{};
    Status rets = XGetWindowAttributes(disp, rootWin, &attrs);
    assert(rets);
    std::cout << "Root window size is: " << attrs.width << "x" << attrs.height << '\n';

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

    glVertexAttribPointer(VERT_ATTRIB_VERT_COORD, 3, GL_FLOAT, false, sizeof(float)*5, (void*)0);
    glEnableVertexAttribArray(VERT_ATTRIB_VERT_COORD);
    glVertexAttribPointer(VERT_ATTRIB_TEX_COORD, 2, GL_FLOAT, false, sizeof(float)*5, (void*)(sizeof(float)*3));
    glEnableVertexAttribArray(VERT_ATTRIB_TEX_COORD);

    //------------------------------------------------------------

    uint selectionShader = createShaderProg(selectionVertShaderSrc, selectionFragShaderSrc);

    float selVertCoords[12] = {};

    uint selectionVao{};
    glGenVertexArrays(1, &selectionVao);
    glBindVertexArray(selectionVao);

    uint selectionVbo{};
    glGenBuffers(1, &selectionVbo);
    glBindBuffer(GL_ARRAY_BUFFER, selectionVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(imgVertCoords), nullptr, GL_DYNAMIC_DRAW);

    uint selectionEbo{};
    glGenBuffers(1, &selectionEbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, selectionEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(vertIndices), vertIndices, GL_STATIC_DRAW);

    glVertexAttribPointer(VERT_ATTRIB_VERT_COORD, 3, GL_FLOAT, false, 0, (void*)0);
    glEnableVertexAttribArray(VERT_ATTRIB_VERT_COORD);

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

    glPolygonMode(GL_FRONT_AND_BACK, false ? GL_LINE : GL_FILL);

    bool isDragging = false;
    int mouseX{};
    int mouseY{};
    int selStartX{};
    int selStartY{};
    bool done = false;
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
                        done = true;
                    break;
                }

                case ClientMessage:
                    // If the message is "WM_DELETE_WINDOW"
                    if ((Atom)(event.xclient.data.l[0]) == wmDeleteMessage)
                        done = true;
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
            const float x1 = float(selStartX)/sshot.getWidth()*2-1.0f;
            const float y1 = float(sshot.getHeight()-selStartY)/sshot.getHeight()*2-1.0f;
            const float x2 = float(mouseX)/sshot.getWidth()*2-1.0f;
            const float y2 = float(sshot.getHeight()-mouseY)/sshot.getHeight()*2-1.0f;
            selVertCoords[0]  = x1; selVertCoords[1]  = y1;
            selVertCoords[3]  = x1; selVertCoords[4]  = y2;
            selVertCoords[6]  = x2; selVertCoords[7]  = y1;
            selVertCoords[9]  = x2; selVertCoords[10] = y2;

            glBindBuffer(GL_ARRAY_BUFFER, selectionVbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(imgVertCoords), selVertCoords);
        }
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

        glXSwapBuffers(disp, glxWin);
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
    return 0;
}
