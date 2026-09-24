# Vendored third-party code

## sx126x_driver

| Item | Value |
|---|---|
| Upstream | https://github.com/Lora-net/sx126x_driver |
| Tag | v2.5.0 |
| Commit | a10c5dfdf89788c6ac805e9fe98889de44175aa2 |
| Archive | https://codeload.github.com/Lora-net/sx126x_driver/zip/refs/tags/v2.5.0 |
| Archive SHA-256 | 95E83D372674807CFB499FDA3C8AB9BC9B6EFE2D68211595F3BE89A2AFB188D8 |
| Retrieved | 2026-09-24 |
| License | The Clear BSD License, Semtech Corporation (see `sx126x_driver/LICENSE.txt`) |

Files are copied unmodified (`LICENSE.txt`, `README.md`, `CHANGELOG.md`, `src/*`).
Only `src/sx126x.c` and `src/sx126x_driver_version.c` are compiled; the BPSK and LR-FHSS
extensions are present but not built. The upstream `src/CMakeLists.txt` is not used by ESP-IDF.

The HAL required by the driver (`sx126x_hal_write/read/reset/wakeup`) is implemented by
CowNect in `../sx1262_hal_espidf.cpp`. The vendor headers are a PRIVATE include directory of the
`lora_radio` component, so no application code depends on Semtech types.

Do not edit files under `sx126x_driver/`. To update: replace the folder with a new pinned
release and update this table.
