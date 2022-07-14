# Screenshotter

Lightweight screenshot taker program for Unix systems running X.

## Dependencies
Note: Almost all of these are already installed on most Linux systems.
* X11
* Xlib
* GLX
* GLEW
* libpng16
* libz
* libnotify
* GTK 2.0

## Building

### Step 1: Install dependencies

Command for Debian:
```sh
sudo apt install libx11-dev libglx-dev libglew-dev libpng-dev libz3-dev libnotify-dev libgtk2.0-dev
```

### Step 2: Clone repo
```sh
git clone git@github.com:timre13/screenshotter.git
```

### Step 3: Build
In the project directory:
```sh
mkdir build
cd build
cmake ..
make
```
The output will be the binary `shot`

### Step 4: 
Configure your window manager to run the executable when pressing the PrintScreen key.

Example for i3wm:
Add the line `bindsym Print exec /path/to/shot/exe` to `~/.config/i3/config`
