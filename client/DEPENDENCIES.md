# Client build dependencies

The client DLL links/compiles against the libraries below. They are **not** committed to this
repository — obtain the listed versions and place each under `client/libs/` using the exact folder
name shown, then build `AP.vcxproj`. The project references these paths directly, so the names matter.

Most are header-only. The exceptions are **zlib** (its `.c` files are compiled in) and **OpenSSL**
(linked for `wss://` / TLS support — see the OpenSSL note below; it's the one non-trivial dependency).

| Library | Version | License | Source |
| --- | --- | --- | --- |
| apclientpp | `main` snapshot (targets AP protocol 0.6.4) | MIT | https://github.com/black-sliver/apclientpp |
| wswrap | 1.3.0 (`WSWRAP_VERSION` 10300, `main`) | MIT | https://github.com/black-sliver/wswrap |
| Asio (standalone) | 1.28.0 | Boost-1.0 | https://github.com/chriskohlhoff/asio (tag `asio-1-28-0`) |
| nlohmann/json | 3.12.0 | MIT | https://github.com/nlohmann/json (tag `v3.12.0`) |
| WebSocket++ | 0.8.2 | BSD-3-Clause | https://github.com/zaphoyd/websocketpp (tag `0.8.2`) |
| valijson | `master` snapshot | BSD-2-Clause | https://github.com/tristanpenman/valijson |
| zlib | develop snapshot (`ZLIB_VERSION` `"1.3.2.1-motley"`, post-1.3.1; the 1.3.1 release works too) | zlib | https://github.com/madler/zlib |
| OpenSSL | 3.x (built + tested with 3.6.1) | Apache-2.0 | vcpkg `openssl:x64-windows-static-md`, or https://www.openssl.org |

## Required layout

The build (`AP.vcxproj`) expects exactly these paths to exist:

```
client/libs/
├─ apclientpp-main/        -> apclient.hpp                      (include: libs\apclientpp-main)
├─ wswrap-main/            -> include/wswrap.hpp                 (include: libs\wswrap-main\include)
├─ asio-1.28.0/            -> asio/include/asio.hpp              (include: libs\asio-1.28.0\asio\include)
├─ json-develop/          -> single_include/nlohmann/json.hpp   (include: libs\json-develop\single_include)
├─ websocketpp-master/     -> websocketpp/*.hpp                  (include: libs\websocketpp-master)
├─ valijson-master/        -> include/valijson/*.hpp             (include: libs\valijson-master\include)
├─ zlib/                   -> zlib.h + zlib sources             (include: libs\zlib)
└─ openssl/                -> include/openssl/*.h + lib/libssl.lib + lib/libcrypto.lib
                              (include: libs\openssl\include ; link: libs\openssl\lib)
```

zlib source files compiled into the DLL (from `libs/zlib/`): `adler32.c`, `crc32.c`, `deflate.c`,
`inffast.c`, `inflate.c`, `inftrees.c`, `trees.c`, `zutil.c`.

## OpenSSL (for `wss://` / TLS)

The client supports both `ws://` (plaintext) and `wss://` (TLS) servers. TLS needs OpenSSL, which is
**statically linked** so the built `Scotch.dll` is self-contained (no extra runtime DLLs to ship).

The build is configured against the **`x64-windows-static-md`** triplet (static libs built with the
`/MD` runtime, matching this project). The easiest way to get it:

```
vcpkg install openssl:x64-windows-static-md
```

Then copy into `client/libs/openssl/`:
- `include/openssl/`  ← `<vcpkg>/installed/x64-windows-static-md/include/openssl/`
- `lib/libssl.lib`, `lib/libcrypto.lib`  ← `<vcpkg>/installed/x64-windows-static-md/lib/`

`AP.vcxproj` links `libssl.lib;libcrypto.lib;crypt32.lib;ws2_32.lib` (plus the Windows system libs
static OpenSSL pulls in). To build **without** TLS instead (plaintext `ws://` only, no OpenSSL), add
`WSWRAP_NO_SSL` back to the preprocessor definitions and drop the OpenSSL include/lib/links.

> Tip: download each library's release/branch archive and extract it so the folder above contains the
> indicated entry point. Extra files in an archive are harmless. License/attribution for each library is
> in [`../THIRD-PARTY-NOTICES.md`](../THIRD-PARTY-NOTICES.md).
