# Math Convention

## World coordinate system

The world is **right handed**.

ex) When looking at the monitor, if right is +X and up is +Y, then outward from the monitor is +Z.

## World units

World position unit is **centimeter**.

Sun light is defined by **illuminance** and its unit is **lux**. [(Reference)](https://www.realtimerendering.com/blog/physical-units-for-lights/)

## Winding order

Meshes are assumed to be **backface culled** and **front face is counterclockwise**.

## Matrix multiplication order

C++  : M2 * M1 * v
HLSL : mul(mul(v, M1), M2)

-> vector v is multiplied by matrix M1 and then M2.
