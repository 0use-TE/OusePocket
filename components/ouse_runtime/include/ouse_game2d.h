// Bounded, allocation-free game kernels shared by the firmware and host tests.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Projectile: x,y,vx,vy,life,radius,enabled,excluded-target-bitmask (8 doubles).
// Target: x,y,radius,enabled (4 doubles). Hits: one uint32_t per projectile.
// Integrates active projectiles, then tests the complete swept segment. Strict
// overlap matches the JS engine. Results follow target index, not hit distance.
// Invalid input leaves every output unchanged. Buffers must not overlap.
// Byte buffers may be unaligned (ESP32-P4's JS allocator aligns to 4 bytes).
bool ouse_game2d_projectiles(void *shots,size_t shot_count,const void *targets,
    size_t target_count,void *hits,double dt);
#ifdef __cplusplus
}
#endif
