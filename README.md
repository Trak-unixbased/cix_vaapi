# CIX VAAPI Driver
## How to build
```bash
mkdir build
cd build
cmake ..
make
```

Copy the generated `libcix_va_drv_video.so` to `/usr/local/lib/aarch64-linux-gnu/dri/`

### Cross-compile for aarch64
```bash
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../cc.cmake ..
make
```

### Debug build and turn off optimization
```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS_DEBUG="-g -O0" -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" ..
```

## Supported codecs
- **Decode**: H.264, HEVC, VP9, AV1
- **Encode**: H.264, HEVC
- **VPP**: Format conversion, scaling, overlay (via CME)

## Log tool
Log levels
- 0 - ERROR
- 1 - WARNING
- 2 - INFO
- 3 - DEBUG

Default WARNING

Set environment variable `CIX_VAAPI_LOG` to change log level.

```bash
export CIX_VAAPI_LOG=3
```

## Dump tool
Use env variables to enable and disable dump feature. Dump file name will be generated in following pattern.

- cix-vaapi-dump-%Y%m%d-%H%M%S.yuv for frame dump;
- cix-vaapi-dump-%Y%m%d-%H%M%S.bin for bitstream dump.
### Dump bitstream
```bash
export CIX_VAAPI_DUMP_BITS=1
```
### Dump frame buffers
```bash
export CIX_VAAPI_DUMP_FRAME=1
```
