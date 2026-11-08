#pragma once
#include <nds.h>
bool sky_load(int n);              // sky<n>.ssxi; sets the fog (horizon) and clear (zenith) colours
void sky_free(void);
void sky_draw(const float cam[3]); // game units; call after the camera matrix, before the course
