# MinHook

x64 source subset from [MinHook v1.3.4](https://github.com/TsudaKageyu/minhook/tree/c3fcafdc10146beb5919319d0683e44e3c30d537), commit `c3fcafdc10146beb5919319d0683e44e3c30d537`.
Downloaded from the official repository on 2026-09-18 UTC. See LICENSE.txt and the per-file notices, including HDE's license. The project builds these C sources statically; no third-party DLL is placed in the game installation.

Local changes: `buffer.c` searches within 0x70000000 bytes instead of 0x40000000, because the observed game had no usable allocation-granularity-aligned free region in the default range. `trampoline.c` explicitly rejects relocated RIP-relative operands outside signed 32-bit range rather than truncating. Both edits are marked in source. All other upstream files are unchanged.
