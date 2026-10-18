HIT & RUN: UNLEASHED
====================
Version 1.0.0

A weapon wheel, three weapons and two items for The Simpsons: Hit & Run (PC):
a HANDGUN, a pump-action SHOTGUN, a ROCKET LAUNCHER (the RPG), a DONUT and a
BEER (with drunk mode). An unofficial fan-made mod.

Source code and updates:
  https://github.com/philparkinson1204/hit-and-run-unleashed

REQUIREMENTS
  * The Simpsons: Hit & Run for PC, English release. On other releases
    (International, Best Seller Series, the demo) the wheel and the weapons
    appear, but shots and rockets don't hit anything.
  * Windows 10 or 11. On Windows 7 only keyboard and mouse work. Linux, Steam
    Deck and Mac need a DLL override (see TROUBLESHOOTING).
  * For a controller: Lucas' Simpsons Hit & Run Mod Launcher, with its "XInput"
    hack on - https://modbakery.donutteam.com/projects/view/6
    Keyboard and mouse work without it.

INSTALL
  1. Extract the ZIP straight into your game folder, the one with Simpsons.exe
     in it. You should end up with:
       <game folder>\Simpsons.exe     (the game's own)
       <game folder>\d3d8.dll         (the mod)
       <game folder>\WeaponWheel\     (this folder)
  2. With Lucas' Mod Launcher: turn its "XInput" hack ON for a controller, and
     keep its "Direct3D 9" hack OFF (the mod can't start with it on, and tells
     you if it is).
  3. Start the game as usual, through the Mod Launcher or Simpsons.exe. On
     foot, hold LB (or TAB) to open the wheel.
  No game files come with the mod, and none are changed. It reads the art and
  sounds it borrows (digits, fonts and menu sounds) from your copy of the game,
  and builds Homer's skin points (for the drunk falls) and each character's
  chin points (for the shotgun) from it when the game starts.

THE WHEEL
  * Hold LB on foot (not in a car, a menu or a cutscene) to open the wheel.
    The game slows down and blurs behind it, with the game's own menu sounds.
  * Move either stick to highlight a slot (D-pad LEFT / RIGHT steps round them
    too). RB flips between the WEAPONS and ITEMS pages. Let go of LB to equip
    the highlighted slot.
  * WEAPONS: 1 FISTS (top), 2 the HANDGUN (left), 3 the RPG, the rocket
    launcher (bottom), 4 the SHOTGUN (right).
  * ITEMS: 5 the DONUT (left), 6 the BEER (right).

WITH THE HANDGUN OUT
  * Hold LT (or D-pad LEFT) to aim over your shoulder. The right stick aims in
    every direction; your character turns, twists and looks where you aim.
  * RT fires: muzzle flash, smoke, a tracer and a flying shell, and the shot
    lands where the crosshair is.
  * Shots hit things the game's own way: dust, grass and sparks; bullet holes
    in walls; cars smoke and finally blow up; people get knocked down (or
    cower); hydrants, small trees, stop signs and phone booths break; wasp
    cameras explode. Causing trouble raises the Hit & Run meter just like in
    normal play.
  * Cars keep their bullet holes as they drive off, and clank when hit. Their
    windows crack; the third bullet in the glass (or a rocket) smashes it all.
  * Shoot a tire and it pops and goes flat: the car sinks onto that rim, pulls
    toward it and gets hard to drive, and the rim throws sparks. A rocket
    blows out the tires next to it; a repair (the wrench) fixes them.
  * People go down however far away they are, and everyone near a shot or a
    blast runs from it.
  * Tap RT without aiming for a quick shot straight ahead from the hip.

WITH THE SHOTGUN OUT
  * Your character pulls it out of a pocket, takes it in both hands and racks
    the pump.
  * Aim and fire like the handgun: ten pellets a shot, a big kick, and people
    knocked flying. It's pumped again after every shot.

WITH THE ROCKET LAUNCHER OUT
  * It rides on your right shoulder. Aim the same way (LT or D-pad LEFT) and
    the launcher comes down level with both hands on it.
  * RT fires a real rocket that flies through the world to whatever is under
    the crosshair (or bursts in the air far away). The next rocket loads in
    about 1.6 seconds - you'll see its red nose pop back into the tube.
  * It explodes like a car blowing up. A car it hits is wrecked; anything else
    close by gets damaged, shoved, knocked flying or broken - but walls
    protect whatever is behind them. Blow up enough traffic and the police
    come after you.
  * Cars that a mission protects stay protected, and your own rockets don't
    hurt you.

THE DONUT AND THE BEER
  * Pick one on the ITEMS page (or press 5 or 6) and your character starts
    eating or drinking it. Every pick is another one.
  * The DONUT comes out of a pocket and is eaten in four bites, with crumbs
    and a belly pat. It's just for fun: it has no effect on play.
  * The BEER adds up: after a few cans your character sways, waddles,
    stumbles, trips and falls (and gets back up). Driving gets wobbly, there's
    double vision, and it all wears off over time.
  * Homer, Marge and Apu drink. Bart and Lisa don't get the beer: its slot is
    dimmed for them.

KEYBOARD AND MOUSE
  Everything works with a keyboard and mouse too, and you can switch between
  them and a controller at any time.
  * Hold TAB to open the wheel and move the mouse toward a slot; let go to
    equip. The scroll wheel steps round the slots, Q flips the page.
  * 1, 2, 3, 4 pick fists, the handgun, the RPG or the shotgun straight away;
    5 and 6 the donut and the beer. The scroll wheel goes through the weapons
    - one step for each flick of the wheel; Q swaps back to the one you had
    before.
  * Hold the RIGHT mouse button to aim; the mouse moves the aim one to one (no
    smoothing, no acceleration). LEFT mouse button fires (or shoots from the
    hip).
  * A quick right-click keeps you aiming, so you can click to shoot - made for
    trackpads (a two-finger click is a right-click). Right-click again to
    stop. Holding works as usual.
  * While you play with the mouse the pointer stays inside the game window, so
    it can't wander onto the taskbar or the Dock (menus and Esc give it back).
  * With a weapon picked the mouse buttons belong to the gun (in a car too), so
    use E to get in and out of cars, talk and open doors, and F to kick. (They
    work with fists too; in a car F is the handbrake. With fists picked, the
    mouse buttons do what they always did in the game.)
  * Letters and numbers are keys by their place on the keyboard, like the
    game's own keys: on a French (AZERTY) keyboard the Q key is the one marked
    A.
  * WASD, Shift, Space and the camera are the game's own, as always.
  * Mac (Wine): macOS adds its own mouse acceleration, so aiming won't feel
    quite as raw as on Windows. Turning down the tracking speed / "pointer
    acceleration" in System Settings helps. MouseSensitivity in the ini sets
    the speed.

ALL WEAPONS
  * When you let go of aim, the normal camera is already behind you.
  * The weapon is put away by itself in cars and cutscenes and comes back out
    after. Shooting from cars isn't in this release.
  * To put it away, pick FISTS (slot 1 in the wheel, or the 1 key).

SETTINGS
  Open WeaponWheel.ini (in this folder) with Notepad. Buttons:
      WheelButton=LB
      AimButton=DPAD_LEFT LT
      FireButton=RT
  Button names: A B X Y LB RB LT RT LS RS BACK START DPAD_UP DPAD_DOWN
  DPAD_LEFT DPAD_RIGHT. Put a space between names to allow more than one
  button.
  Keyboard and mouse keys (KeyWheel=TAB, KeyAim=MOUSE2, KeyFire=MOUSE1, ...)
  and MouseSensitivity are listed in the ini with the key names.
  Also: Volume, AimSensitivity, InvertY, AimDeadzone (raise it if the aim
  drifts by itself), SlowMotion, Blur, Brightness, Size.
  Leave Debug at 0. Save the file and restart the game.

KNOWN LIMITATIONS
  * Long hip-fired rockets can pass through some walls: aim first (LT or the
    right mouse button) for reliable hits.
  * In tight spaces (indoors, pressed against a car) the over-the-shoulder
    camera can clip into your character.
  * Fullscreen mode is untested (it was tested windowed, from 800x600 to
    1920x1080 and ultrawide).
  * The full list is in the README on the project page (the link at the top).

UNINSTALL
  1. Close the game.
  2. In the game folder (where Simpsons.exe is) delete:
       d3d8.dll
       the WeaponWheel folder (this one)
  That's everything - the mod doesn't change any other game files.

TROUBLESHOOTING
  WeaponWheel.log in this folder says what the mod saw (game version,
  controller, keys, sounds and art). Its first line reads "SHAR Weapon Wheel
  1.0.0 loaded" - the mod's internal name. It's written again every time the
  game starts: copy it before you start again (to send it with a bug report,
  for example).
  * Nothing happens and there's no WeaponWheel.log: d3d8.dll isn't next to
    Simpsons.exe, or your antivirus removed it (see below). The game's exe
    must be named Simpsons.exe.
  * A message says the Direct3D 9 hack is on: turn it off in the Mod Launcher
    (Hacks) and start the game again.
  * The controller doesn't open the wheel: turn the Mod Launcher's "XInput"
    hack on.
  * A key does nothing: the log's "Keys:" lines list every key in use, and
    anything in WeaponWheel.ini it didn't understand.
  * Text or sounds missing: the mod reads them from the game folder
    (art\frontend and soundfx.rcf). The log's "Game art" and "Sounds" lines
    say what it found.
  * Shots and rockets don't hit anything: you're on a non-English release
    (see REQUIREMENTS).
  * Homer doesn't fall over when drunk: the log says "dbody: no skin points" -
    the mod couldn't read art\chars\homer_m.p3d or homer_a.p3d from your game,
    so Homer gets the simpler drunk sway the other characters have.
  * A character's chin sits in the shotgun's stock: the log's "chin points"
    line names a model it couldn't read from art\chars (or one replaced by a
    different model).
  * Linux, Steam Deck, Mac (Wine, Proton, CrossOver): the game folder's
    d3d8.dll is only used with a DLL override: d3d8 = "native, builtin"
    (winecfg > Libraries, or the launch option
    WINEDLLOVERRIDES="d3d8=n,b" %command%  on Steam).
  * Another mod that is also a d3d8.dll (dgVoodoo, d3d8to9, ReShade for
    Direct3D 8) can't run next to this one: there can only be one d3d8.dll.
  * The game is under C:\Program Files: Windows may keep the log in
    %LOCALAPPDATA%\VirtualStore instead, and Notepad may need "Run as
    administrator" to save WeaponWheel.ini.
  * Antivirus: some antivirus programs (Windows Defender included) can wrongly
    flag d3d8.dll, because it hooks into the game the way overlays and
    trainers do. It doesn't touch anything outside the game, and its full
    source code is public, so anyone can check it or build it themselves. If
    d3d8.dll disappears after you copy it in, look in the antivirus's
    protection history and restore it.

LICENSE
  The mod's own code, art and sounds are released under the MIT License.
  Credits, the license and the third-party notices are in
  LICENSE-AND-NOTICES.txt in this folder.
