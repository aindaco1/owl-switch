# OwlSwitch

**A retro video controller — the owls are not what they seem.**

OwlSwitch is a macOS Apple Silicon fork of 240-MP with a CRT/VHS-style interface,
keyboard and remote navigation, and independent controller and media displays.
It browses your media and launches mpv for playback. Packaged apps include all
required helpers; end users do not need Homebrew or separate player installs.

## Install

Download the latest `owl-switch-<tag>-macOS-arm64.dmg` from
[GitHub Releases](https://github.com/aindaco1/owl-switch/releases/latest), open it,
and drag `OwlSwitch.app` onto the Applications shortcut.

See [installation, updates, and uninstalling](docs/INSTALL.md) for requirements
and details. The app checks for updates on launch; downloads and installation
require your action through **Settings → Software Update**.

OwlSwitch supports Apple Silicon macOS. Intel macOS, Linux, and Raspberry Pi
packaging are outside this fork's scope.

## Modules

The home screen presents these six modules in order:

| Module | What it does |
|---|---|
| Jellyfin | Movies and TV with password/Quick Connect sign-in, Continue Watching, Up Next, resume, track selection, and direct or transcoded playback. |
| Karaoke | Search eighteen YouTube sources and edit a persistent queue while songs play on the media display. Includes next-song previews, prefetch, and transitions. |
| Retro | Browse MyRetroTVs decade feeds from the 50s through the 00s, surf channels, and apply CRT effects. |
| Tumblr | Play a shuffled still-image/GIF montage from public Tumblr blogs, with saved favorites. |
| Nature | Show CC0 iNaturalist photos with species/place labels and independent CC0 Earth Garden/Freesound audio, using five-second crossfades. |
| Local | Browse videos, images, and audio; manage persistent media and soundtrack queues, repeat/shuffle, resume, subtitles, and YouTube soundtrack playlist imports. |

Local includes the former Loop workflows. Plex remains hidden as a reference
implementation. See the [architecture guide](docs/ARCHITECTURE.md) for module
behavior, data contracts, and implementation details.

## Controls

Use arrow keys, Enter, Escape/Backspace, or a Right Shift tap for Back. Native
macOS GameController navigation is supported. **Settings → Controls** adds a
keyboard or keyboard-emulating remote button to each navigation action while
keeping the built-in controls available.

In Nature, Space/Enter pauses images and sound together; Right advances the
image, R refreshes observations, I opens the photo source, A opens the recording
on Freesound, and Back stops playback. **Settings → Nature** controls sound
(default ON) and volume (default 30%). Sounds rotate independently to their
natural end; an unavailable sound stream leaves the slideshow running. Nature
caches metadata only and requires internet access to load images and audio.

## Documentation and Development

Start with the [documentation index](docs/README.md). It links the installation,
architecture, build/release, contribution, security, changelog, roadmap, and plan
records. Developers should follow [Building OwlSwitch](docs/BUILDING.md) and
[Contributing](docs/CONTRIBUTING.md).

Settings, authentication, queues, and caches live outside the app bundle; see the
[data inventory](docs/ARCHITECTURE.md#config-storage). For credential handling,
diagnostics privacy, and reporting vulnerabilities, see the
[security policy](docs/SECURITY.md).

## License

This project remains licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for the full text.

The bundled VCR OSD Mono font is by Riciery Santos Leal (mrmanet); its license is in [assets/fonts/LICENSE-vcr-osd-mono.txt](assets/fonts/LICENSE-vcr-osd-mono.txt). GNU Unifont provides fallback glyphs for CJK, Hangul, and other scripts under the SIL Open Font License v1.1; see [assets/fonts/LICENSE-unifont.txt](assets/fonts/LICENSE-unifont.txt).

If you distribute a modified version, you must also distribute it under GPL-3.0 and make the source available.
