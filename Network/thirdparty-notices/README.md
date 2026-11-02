# Network dependency notices

The standalone networking build now fetches **Mbed TLS 3.6.7**, including its
bundled source dependencies, from the official release archive:

https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7

Archive SHA-256:
`a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`

Open Rune selects the **Apache-2.0** option for the dual-licensed Mbed TLS code.
The unmodified upstream full license is retained in `MbedTLS-LICENSE.txt`.
Copyright: The Mbed TLS Contributors. The bundled framework license is retained
separately; it concerns build/test support. No dependency source was modified.

The upstream notices for Project Everest (Apache-2.0, copyright INRIA and
Microsoft Corporation) and p256-m (author Manuel Pégourié-Gonnard, Apache-2.0
option) are retained as the original README files alongside the full license.
They are included conservatively with the release's bundled components; this
does not assert that every optional algorithm is enabled in the compiled binary.

The default configuration builds several algorithms, but the Open Rune channel
permits only DTLS 1.2 with ECDHE-PSK / ChaCha20-Poly1305 and the Open Rune ALPN.
The upstream crypto code is a dependency, not original Open Rune code.

Standalone binary distributions must carry this notices folder. CMake copies it
to `network-notices/` beside the standalone build products. The Android APK does
not yet link this network library; when it does, its existing notice packager
must incorporate these notices too. Do not claim the current APK contains DTLS.
