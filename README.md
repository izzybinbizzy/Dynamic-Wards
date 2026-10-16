# Dynamic Wards - SKSE plugin

Copyright (C) 2026 izzydoingit. GPL-3.0-or-later, see `LICENSE`.

The plugin of Dynamic Wards; it ships with the mod and is required by it.

## What it does
- One neutral set of ward meshes, colored in memory: any color for any ward, a ladder per rank, opacity and
  transparency (the palettes are rebuilt on the graphics card, the glow set on the cached model). No plugin file,
  nothing in the save.
- The ward lights, made by the plugin on Community Shaders, ENB or Vanilla.
- A pick for each shield that raises a ward (Spellbreaker, the Shield of the Crusader, the Shield of Reman Cyrodiil):
  off (no ward), the normal dome or the 360 dome.

## Requires
SKSE and Address Library for SKSE Plugins; SKSE Menu Framework for the menu. The 360 dome needs 360 Ward and
360 Ward Universal Patch SKSE.

## Settings
SKSE Menu Framework > Dynamic Wards, saved to `DynamicWards.ini` at once.

## Third-party files
`src/DevBenchAPI.h` and `src/DevBenchAPI.cpp` are devbench's own interface files, MIT licensed by
their author (see `src/DevBenchAPI.LICENSE.txt`) and copied unchanged. devbench is not required.

`src/SKSEMenuFramework.h` is SKSE Menu Framework's own header by Thiago Kaique, copied unchanged from
[SKSE-Menu-Framework-3-Example](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-Example), MIT licensed
(see `src/SKSEMenuFramework.LICENSE.txt`).
