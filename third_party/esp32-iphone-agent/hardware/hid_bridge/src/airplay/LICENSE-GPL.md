# License of this directory (GPL-3.0)

Everything under `hardware/hid_bridge/src/airplay/` is a port of the AirPlay receiver in
[UxPlay](https://github.com/FDH2/UxPlay) (`lib/`), itself derived from RPiPlay and
playfair. UxPlay is distributed under the **GNU General Public License v3.0**, so these
files are GPL-3.0 as well (see https://www.gnu.org/licenses/gpl-3.0.html; `playfair/LICENSE.md`
carries the full text).

The rest of this repository stays under its own license. Note that the firmware binary
that links this directory is a combined work and is therefore distributed under GPL-3.0.

Ported files: `rtsp.c` (httpd/raop), `handlers.c` (raop_handlers.h), `pairing.c`
(pairing.c/crypto.c), `fairplay.c` (fairplay_playfair.c), `mirror.c` (raop_rtp_mirror.c,
mirror_buffer.c, raop_ntp.c), `mdns.c` (dnssd), `playfair/` (unchanged except that the
lookup tables are `const` so they live in flash).
