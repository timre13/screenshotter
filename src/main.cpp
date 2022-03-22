#include <cassert>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <fstream>
#include <iostream>

/*
 * http://www.verycomputer.com/275_6ac8f0955e9280fa_1.htm
 */

int main()
{
    Display* disp = XOpenDisplay(nullptr);
    assert(disp);
    Screen* screen = XDefaultScreenOfDisplay(disp);
    int screeni = XDefaultScreen(disp);
    assert(screen);
    Window win = XRootWindowOfScreen(screen);



    XWindowAttributes attrs{};
    Status rets = XGetWindowAttributes(disp, win, &attrs);
    assert(rets);

    XShmSegmentInfo shminfo{};

    XImage* img = XShmCreateImage(
            disp,
            DefaultVisual(disp, screeni),
            DefaultDepthOfScreen(screen),
            ZPixmap,
            nullptr,
            &shminfo,
            attrs.width,
            attrs.height
    );
    shminfo.shmid = shmget(IPC_PRIVATE, img->bytes_per_line*img->height, IPC_CREAT|0777);
    img->data = (char*)shmat(shminfo.shmid, nullptr, 0);
    shminfo.shmaddr = img->data;
    shminfo.readOnly = false;

    Bool retb = XShmAttach(disp, &shminfo);
    assert(retb);

    retb = XShmGetImage(disp, win, img, 0, 0, AllPlanes);
    assert(retb);


    std::fstream file;
    file.open("out.ppm", std::ios_base::out|std::ios_base::binary);
    assert(file.is_open());

    file.write("P6\n", 3);
    const std::string widthStr = std::to_string(img->width) + "\n";
    file.write(widthStr.c_str(), widthStr.length());
    const std::string heightStr = std::to_string(img->height) + "\n";
    file.write(heightStr.c_str(), heightStr.length());
    file.write("255\n", 4);
    for (int i{}; i < img->width*img->height; ++i)
    {
        file.put(img->data[i*4+2]);
        file.put(img->data[i*4+1]);
        file.put(img->data[i*4+0]);
    }
    file.close();

    std::cout << "Saved screenshot to \"out.ppm\"\n";

    XShmDetach(disp, &shminfo);
    shmdt(shminfo.shmaddr);
    shmctl(shminfo.shmid, IPC_RMID, 0);

    return 0;
}
