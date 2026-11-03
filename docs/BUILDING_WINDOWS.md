# Building on Windows

There are two kinds of build, described in [BUILDING.md](../BUILDING.md):

| Build | What you get | Needs Linux? |
| --- | --- | --- |
| **Host build** (desktop window or browser) | The firmware running on your computer against a simulated FM-1: screen, panel, sound, MIDI in. | No, but Linux (WSL) is the tested way |
| **FM-1 build** | `.fwsc` package to install on the real FM-1 | **Yes**: JieLi's compiler only runs on Linux x86-64 (WSL or Docker) |

### The web pages do not replace the build

Midi-Mania's web pages don't compile anything:
- the **web installer** puts an already-built `.fwsc` onto the FM-1 over USB;
- **Midi-Mania Studio**, the browser version, runs a prebuilt Midi-Mania and loads your MIDI files into it.

To run *our* firmware anywhere, we have to build it, and the FM-1 build needs Linux.

## Recommended: WSL 2 (Ubuntu)

WSL is not installed on this machine yet. In an administrator PowerShell:

```
wsl --install -d Ubuntu
```

Restart, open **Ubuntu** from the Start menu, then:

```
sudo apt update
sudo apt install -y build-essential git python3 python3-pil python3-fonttools \
                    libsdl2-dev libasound2-dev nodejs
git clone <this repository's URL> ~/fm-1-midi-mania
```

Clone into the Linux home folder (`~`), not `/mnt/c/...`: building on the Windows drive from WSL is many times slower.
VS Code (and VSCodium with the open-source WSL extension) opens that folder from Windows.

For the FM-1 build, also (see [BUILDING.md](../BUILDING.md)):

```
cd ~/fm-1-midi-mania
tools/get_toolchain.sh                         # JieLi's compiler, into ~/.jieli/toolchain
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
```

## Testing on Windows when building in WSL

Yes, everything is still tested from Windows:

| What | How |
| --- | --- |
| **Desktop build** (SDL2) | Run it inside WSL. Windows 11 shows Linux windows and plays their sound (WSLg), no setup needed. |
| **Browser build** | Serve it from WSL (`python3 -m http.server -d build/web 8790`) and open `http://localhost:8790` in Chrome or Edge on Windows. WSL 2 forwards `localhost`. |
| **MIDI keyboard into the browser build** | Works: Chrome on Windows sees your USB MIDI devices (Web MIDI). |
| **The real FM-1** | Build the `.fwsc` in WSL, then install it **from Windows**: serve a local copy of the installer page from WSL (`web/make_site.py`, BUILDING.md "Install") and open it in Chrome or Edge on Windows. The browser talks to the FM-1 over USB, so WSL never needs to see the USB device. |
| **The files** | WSL's files are in Explorer under `\\wsl$\Ubuntu\home\<you>\`. |

The command-line installer (`tools/fm1_install.py`) needs to see the FM-1's USB MIDI port. Run it with Windows
Python (`pip install mido python-rtmidi`), not inside WSL. Inside WSL it would need USB passthrough (`usbipd`).

## Without WSL (not tried)

This machine has MSYS2 (`/ucrt64` is on the PATH). The host build might work natively in the MSYS2 UCRT64 shell:

```
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-python-pillow \
          mingw-w64-ucrt-x86_64-python-fonttools
```

Felucca has only tested its SDL build on macOS (Linux next), so expect fixes. The FM-1 build still needs Linux.

## The browser build

The browser build runs in Chrome or Edge on Windows, so WSLg is not involved, and it takes a USB MIDI keyboard
(Web MIDI). It needs Emscripten, installed once into Ubuntu's home folder (no password, about 1.7 GB, Node.js
included):

```
git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
```

Then, in each new Ubuntu terminal, load it, build, and serve the page:

```
cd ~/emsdk && . ./emsdk_env.sh && cd /mnt/c/Users/<you>/Documents/Dev/fm-1-midi-mania
./build.sh web                                   # -> build/web
python3 -m http.server -d build/web 8790         # then open http://localhost:8790 on Windows
```

Leave the server running while you play; stop it with Ctrl+C. After a rebuild, reload the page.

### If the desktop build shows no window

WSLg's display server (Weston) can crash: the program then hangs with no window, and Ctrl+C reports a "fatal IO
error" on X server ":0". `/mnt/wslg/stderr.log` shows "terminated with signal 11" when this happened. Restart WSL
(`wsl --shutdown` in PowerShell) or Windows, or use the browser build.

## The full test suite from the Windows folder

`tests/run_tests.sh` opens, compiles and writes thousands of files. Run from the Windows folder (`/mnt/c/...`), every
one of them crosses the bridge between Windows and WSL, and it takes over an hour. Run it in a copy on WSL's own disk
instead:

```
cd /mnt/c/Users/<you>/Documents/Dev/fm-1-midi-mania
sh tools/wsl_test_copy.sh
```

This mirrors the folder to `~/fm-1-midi-mania-test` (only the changed files; `build/`, `Felucca/` and `.git/` are
skipped), builds there, runs the timing-judge test, then the suite. The log is
`~/fm-1-midi-mania-test/build/run_tests.log`. The copy is overwritten every run: never edit it. Builds and quick checks
are fine from the Windows folder.

## Our own tests

The game logic also builds on its own, with any C compiler:

```
mkdir -p build
cc -std=gnu99 -Wall -Wextra tests/judge_test.c -o build/judge_test && build/judge_test
```
