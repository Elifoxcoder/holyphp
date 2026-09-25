# HolyPHP — Linux installers (.deb + AppImage)

Both packages ship **the compiler only**: the `hphp` ELF binary with the
runtime and standard library embedded. No libraries — `ui` / `websocket` are
installed through the package manager afterwards:

```bash
hphp install ui
hphp install websocket
```

## Build (on Linux or WSL)

```bash
bash packaging/linux/make-deb.sh        # -> packaging/linux/out/holyphp_1.0.0_amd64.deb
bash packaging/linux/make-appimage.sh   # -> packaging/linux/out/HolyPHP-1.0.0-x86_64.AppImage
```

Both scripts build `./hphp` themselves if it's missing
(`packaging/linux/build-linux.sh` — the gcc-driver twin of `scripts/build.sh`,
which is Windows/MinGW-only).

## The `.deb` — system install

```bash
sudo apt install ./holyphp_1.0.0_amd64.deb
```

| Installs | Role |
|---|---|
| `/usr/bin/hphp` | The compiler — `/usr/bin` is on PATH for every user, so `hphp run hello.hphp` works immediately |
| `/usr/lib/holyphp/common-install-wizard.sh` | Wizard steps run by `postinst` |
| `/usr/share/doc/holyphp/README` | Project readme |

After unpacking, `postinst` plays the wizard: verifies the binary runs on this
machine, checks for `gcc` (compiled programs need it — it prints
`sudo apt install build-essential` when missing), and prints the
`hphp pkg install` hints for the libraries. `prerm` reminds you that packages
installed via the manager live in `~/.local/share/hphp` and survive removal.

## The AppImage — portable + optional install wizard

```bash
./HolyPHP-1.0.0-x86_64.AppImage run hello.hphp   # zero-install: runs from anywhere
./HolyPHP-1.0.0-x86_64.AppImage install          # wizard: adds to PATH, checks gcc
./HolyPHP-1.0.0-x86_64.AppImage uninstall        # undo the PATH entries
```

`install` (in `AppDir/AppRun`) prepends the AppImage's own directory to `PATH`
in `~/.profile` and `~/.bashrc` — idempotent, marker-guarded, no admin rights —
so plain `hphp ...` works in new shells. It also verifies the bundled binary,
warns about missing gcc, and prints the package-manager hints. With
AppImageLauncher installed, first double-click offers menu integration too.

## Layout

```
packaging/linux/
  build-linux.sh                 native ELF build (gcc driver link)
  make-deb.sh                    stages the tree + dpkg-deb --build
  make-appimage.sh               stages AppDir + appimagetool
  common-install-wizard.sh       shared wizard steps (verify/path/gcc/libs)
  debian/DEBIAN/{control,postinst,prerm}
  AppDir/{AppRun,hphp.desktop}   AppImage entry points
  hphp.png                       optional 256x256 icon
  out/                           build artifacts
```
