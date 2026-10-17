SPIDERBOT WIFI CONTROLLER
==========================

This version does NOT use USB serial for control.

The ESP32 creates its own WiFi network:

SSID:
    spiderspider

Password:
    spiderspider

Robot IP:
    192.168.4.1

TCP:
    5000

SETUP
-----

1. Upload SpiderBot_WiFi.ino to the ESP32.

2. Power the ESP32 and servo power system.

3. On Windows, open WiFi.

4. Connect to:
       spiderspider

   Password:
       spiderspider

5. Run:
       python SpiderBot_Controller_WiFi.py

6. Press Connect.

The application connects directly to the ESP32 over WiFi.

NO COM PORT IS REQUIRED.

CONTROLS
--------

W       Forward
S       Back
A       Turn left
D       Turn right
Q       Rotate left in place
E       Rotate right in place
Space   Stop

GUI:
    Forward
    Back
    Turn left/right
    Rotate left/right
    Stand
    Sit
    Home
    Wave left
    Wave right

STARTUP
-------

The robot first moves to a low crouch.

It then slowly extends all four legs together for about 2.5 seconds.

This is intentional. Do not extend one leg/servo at a time because that can shift almost the entire load onto the remaining legs.

INSTALL
-------

No external Python package is required.

The controller uses only Python standard-library modules:
    socket
    threading
    tkinter

POWER
-----

Do NOT power the 12 servos from the ESP32 5V pin.

Use a suitable servo power supply/buck converter and connect the servo supply GND to ESP32 GND.

FIRST TEST
----------

Test with the robot physically supported first.

Recommended order:

    Connect
    Stand
    STOP
    Forward
    STOP
    Rotate left
    STOP
    Rotate right
    STOP
    Wave left
    Home

If a leg moves in the wrong direction, correct direction[] or the mechanical servo orientation before changing the gait.
