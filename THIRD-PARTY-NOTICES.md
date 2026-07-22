# Third-Party Notices

The TS2 Redux: Archipelago Edition client DLL (`client/`) is built against the third-party libraries
below. They are obtained per [`client/DEPENDENCIES.md`](client/DEPENDENCIES.md) and are **not** committed
to this repository. Each is used under its own license; all are compatible with this project's GPL-3.0
license. Binary releases of the client (`Scotch.dll`) must carry these notices.

| Library | Version | License | Copyright |
| --- | --- | --- | --- |
| [apclientpp](https://github.com/black-sliver/apclientpp) | `main` (targets AP protocol 0.6.4) | MIT | © 2022–2025 black-sliver, FelicitusNeko, highrow623, NewSoupVi |
| [wswrap](https://github.com/black-sliver/wswrap) | 1.3.0 | MIT | © 2021 black-sliver |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | © 2013–2026 Niels Lohmann |
| [WebSocket++](https://github.com/zaphoyd/websocketpp) | 0.8.2 | BSD-3-Clause | © 2014 Peter Thorson |
| [Asio](https://think-async.com/Asio/) (standalone) | 1.28.0 | Boost Software License 1.0 | © Christopher M. Kohlhoff |
| [valijson](https://github.com/tristanpenman/valijson) | `master` | BSD-2-Clause | © 2016 Tristan Penman; © 2016 Akamai Technologies |
| [zlib](https://zlib.net/) | 1.3.1 (develop `"1.3.2.1-motley"`) | zlib License | © 1995–2026 Jean-loup Gailly and Mark Adler |
| [OpenSSL](https://www.openssl.org/) | 3.x (built with 3.6.1) | Apache-2.0 | © The OpenSSL Project Authors |

Notes:
- `zlib`'s `.c` sources are compiled into the DLL, and **OpenSSL is statically linked**; the other
  libraries are header-only and used via the include paths in `client/AP.vcxproj`.
- TLS (`wss://`) support is provided by the statically-linked OpenSSL, which is embedded in binary
  releases of the DLL — so those releases must carry the OpenSSL (Apache-2.0) notice above.

## Upstream project

This project is a derivative of **TS2 Redux** ([HFTSRedux/TS2Redux](https://github.com/HFTSRedux/TS2Redux)),
a Homefront: The Revolution mod that re-enables the hidden TimeSplitters 2 port, licensed GPL-3.0.
The original Homefront TS2 port is the work of Matt Phillips; the TS2 Redux Tech Team (DevilDwarf,
Fanoto, RyanUKAus, Scotch, Skibbles, Yossarian The Assyrian) produced the Redux mod this builds on.
