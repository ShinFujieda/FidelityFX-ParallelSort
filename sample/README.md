# Sample

## Command Line Arguments

A sample application of the AMD FidelityFX Parallel Sort provides some command line arguments to control settings.

- `-benchmark`: Output .csv file containing performance per on GPU
- `-keyset <0-2>`: 0 - 1920x1080, 1 - 2560x1440, 2 - 3840x2160
- `-payload`: Sort with payload using interleaved 64-bit buffer of key and payload
- `-payload32`: Sort with payload using separated 32-bit buffer
- `-element <uint>`: Set the number of elements per thread
- `-threadgroup <uint>`: Set the thread group size

For example, to sort with payload using 64-bit buffer with indirect execution at 1K with 3x192 blocksize:
```
FFX_ParallelSort_DX12.exe -benchmark -payload -indirect -element 3 -threadgroup 192
```
