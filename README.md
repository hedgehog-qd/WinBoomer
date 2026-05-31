# WinBoomer

WinBoomer is a Windows screen zoomer inspired by [TSoding's Boomer](https://github.com/tsoding/boomer).

It provides the same basic interaction model as the original Boomer: freeze the screen, zoom in/out, drag the frozen image around.  is implemented for Windows using C++ and DirectX, and runs from the system tray.

## Features

- Freeze the current screen
- Zoom with the mouse wheel
- Drag the frozen screen image with the mouse
- Runs in the Windows system tray
- Simple popup MessageBox for changing the trigger hotkey
- Windows-only implementation using C++ and DirectX

## Relationship to TSoding's Boomer

This project is inspired by [Boomer](https://github.com/tsoding/boomer) by TSoding.

The original Boomer is a Linux/X11 zoomer application. WinBoomer is ***not*** an official port and is ***not*** affiliated with TSoding. It is a separate Windows implementation with a similar functionality.

## Usage

1. Launch `WinBoomer.exe`.
2. The app will stay in the system tray.
3. Use the configured hotkey to freeze the screen (`Ctrl + Alt + Z` by default).
4. While the screen is frozen:
   - Use the mouse wheel to zoom in/out.
   - Drag with the left mouse button to move around.
   - Press `Esc` or the configured exit action to return to normal.
   - Press `0` or the configured reset action to reset the zoom.

## Configuration

Right-click the tray icon to open the settings popup.

From there, you can change the trigger hotkey used to activate WinBoomer.

## Building

Requirements:

- Windows
- Visual Studio
- DirectX development environment

Build instructions may vary depending on your local setup or IDE.

## AI Assistance Disclosure

Parts of this project were developed with the assistance of AI tools. The code, design decisions, and final implementation were reviewed by the project author.
