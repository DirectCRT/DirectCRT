DirectCRT @VERSION@
https://github.com/DirectCRT/DirectCRT

DirectCRT is a ddraw.dll replacement for 2D DirectDraw games from the
Windows 98 and XP era. It gives them DOSBox Staging's picture: its
scaling, its colour adjustments and its CRT shading (crt-auto), which
makes them look as they did on a period CRT monitor.


USING IT

1. Copy ddraw.dll into the game's folder, next to its .exe.
2. Start the game.

DirectCRT runs on Windows 10 and 11. Wine and Proton are untested so
far; to try it, set ddraw to "native, builtin" in winecfg (Libraries),
or start the game with WINEDLLOVERRIDES="ddraw=n,b".

DirectCRT only handles DirectDraw: Direct3D games aren't supported. A
game with several renderers needs its DirectDraw or "software" renderer
selected.


SETTINGS

The first time the game starts, DirectCRT creates directcrt.ini next to
ddraw.dll. The settings OSD (Alt+F12) has every setting with its help.
Most changes apply at once, and so do edits to directcrt.ini while the
game runs.

A compatibility setting a game needs goes in its directcrt.ini.
cnc-ddraw's wiki lists the settings many games need (the section is
[directcrt] here, not [ddraw] or one named after the game):
https://github.com/FunkyFr3sh/cnc-ddraw/wiki


HOTKEYS

Alt+Enter                           Switch between a window and fullscreen
Ctrl+Tab, or Right Alt+Right Ctrl   Release the mouse
Alt+Page Down                       Maximise the window
Alt+F11                             Show or hide the status OSD
Alt+F12                             Open or close the settings OSD
Print Screen                        Save a screenshot, in Screenshots\


BUG REPORTS

The DebugLog and DebugLogMini downloads write a log, directcrt-*.log, in
the game's folder. Use one of them in place of this ddraw.dll when
reporting a problem, and attach the log:
https://github.com/DirectCRT/DirectCRT/issues


REMOVING IT

Delete ddraw.dll and directcrt.ini from the game's folder, along with
the files that came with them: DirectCRT-README.txt and
DirectCRT-LICENSE.txt.


LICENCE

DirectCRT is licensed under the GNU General Public License, version 2
or (at your option) any later version. It's a fork of cnc-ddraw by
FunkyFr3sh, and the code that comes from cnc-ddraw is also available
under its original MIT licence. The scaling, colour and CRT shading are
ported from DOSBox Staging (GPL-2.0-or-later). DirectCRT-LICENSE.txt
has the copyrights, these licences and those of the third-party code
and fonts in ddraw.dll, and a link to this build's source code.
