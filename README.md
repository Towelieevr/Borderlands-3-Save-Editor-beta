# Borderlands 3 Save Editor (PS4 / GoldHEN) - beta

In-game save editor plugin for Borderlands 3 on jailbroken PS4s with GoldHEN, by Toweliee/CLAUDE AI.

> **Beta:** back up your saves before using it (Apollo Save Tool or the PS4's save backup). Please report problems on the [Issues](https://github.com/Towelieevr/Borderlands-3-Save-Editor-beta/issues) page.

**Game:** Borderlands 3, **CUSA07823 v1.30** only.

**Tabs:** Character, Item Codes (spawn items from codes, with a level override), SDUs, Cosmetics, Style (menu colours, opacity, font and text size).

## Download

Grab the zip from [Releases](https://github.com/Towelieevr/Borderlands-3-Save-Editor-beta/releases).

If you use the **Toweliee's Save Editors** installer app, choose **Check for updates online** and it will download new versions from this repo by itself.

## Install

1. Copy the `GoldHEN` folder from the zip to `/data/GoldHEN/` on your PS4 over FTP (`bl3_editor.prx` goes in `plugins/`; the icon and `bl3_codes.txt` go in `/data/GoldHEN/`).
2. Add the editor to `/data/GoldHEN/plugins.ini` as shown in the zip's `plugins.ini.txt`. If `XeMo.prx` is listed under `[CUSA07823]`, put the editor line **after** it.
3. Start the game, load into gameplay, and hold **L3 + R3** to open the editor.

## Building

`source/` has the plugin code. `build.py plugin bl3_editor` expects a `tools/` folder next to it with the GoldHEN Plugins SDK and repository, the official PS4 SDK toolchain, and the helper scripts used to wrap the result as a fake-signed prx. None of those are included here.


PLEASE NOTE THIS IS STILL A WORK IN PROGRESS ADVICE IS ALWAYS ACCEPTED THANK YOU FOR YOUR PATIENCE.


## License

Free to use, to modify All i ask is you give some credit or dont up to you.
