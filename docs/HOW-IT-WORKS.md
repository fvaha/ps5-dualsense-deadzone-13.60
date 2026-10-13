# How it works

## The problem

A worn stick does not return to (0,0) at rest. The console believes the stick is held slightly in one direction. The usual software fix is a circular deadzone: anything closer to the center than a radius is snapped to the center. It works, but it costs precision in every direction, including the healthy ones.

Drift usually pushes in one direction. So this filter taxes only that direction with a **cone**: movements inside the cone, up to its range, snap to center. Everything outside passes through untouched.

## Where it lives

```
DualSense ──► libScePad (one copy per process) ──► home screen or game reads the stick
                    ▲
          imported read functions, redirected per process
                    │
              code cave: filter stub (hand-written x86-64, a few hundred bytes)
```

- The home screen and each game map their own copy of `libScePad` into their address space. The payload patches each process separately.
- On the home screen, the controller is read through six imports of that library. The payload finds all six by scanning the process memory for pointers to the original function entries, and redirects them to the stub. Fewer than six means abort: it never partially hooks.
- In a game, the same scan runs on the game's own imports, and the stub is placed in the game's copy of the library.
- The stub lands in an unused padding area of the executable, with its configuration (range, angle, width for each stick) stored right behind the code. The configuration can be rewritten in place later without touching the code.

## The filter

For each controller packet going through a hooked read:

1. Check the original function result, the buffer pointer, the batch bounds and the connected flag of the packet.
2. For each stick, compute `x² + y²` from the two axis bytes, in integers.
3. Outside the squared range: leave the stick alone.
4. Circle mode (width 360): write center.
5. Cone mode: the cone is bounded by two rays. Each ray is a fixed-point normal vector, and the point is inside when its dot product with both normals is non-negative. No division, no floats.
6. Inside: write center. Otherwise leave the stick alone.

The same decision is implemented three times: in the assembly stub, in a C model used by the host tests, and in the JavaScript preview of the UI. The host tests compare the stub and the model over every byte-coordinate pair for a set of configurations.

## Safety checks

Nothing is written before everything is verified:

- **Functions**: the five hooked readers are resolved by name in the target
  process and cross-checked against the library code itself (the two
  ReadState-family and two Read-family entries must be thunks to two distinct
  shared readers, the tracker entry a standalone function). One byte off and
  the patch is refused.
- **Occupancy**: the code cave (found by signature or as the longest all-zero
  gap of the executable image) must be empty or contain this payload's own
  stub. Anything else is refused.
- **Import discipline**: the imports must be all original or all hooked. A mix is refused.
- **Read-back**: every write is read back and compared, including the embedded configuration.
- **Rollback**: if a write fails half-way, the previous bytes are written back and verified.

The home screen is paused with `ptrace` while its imports are swapped. The game is never paused: the stub is written and verified first, then each import pointer is replaced with a single aligned 8-byte write, so a running thread sees either the old or the new function.

Disable restores the original pointers on both sides and leaves the inert stub in the cave. A reboot clears everything in memory.

## Following the game

Once a second, the payload looks for the running game process. When one appears, it checks the fingerprint of its controller library, scans its imports, and applies the current setting. The game status in the UI tells whether the game is supported, patched, and why not otherwise. When the game exits, the state resets.

## Saved setting

Apply writes the setting to `/data/ps5-dualsense-deadzone.conf` after a successful home-screen update. When the payload starts, it reads the file and reapplies the setting to the home screen, then to any game. Disable and Quit clear the file. Errors in reading or writing it are reported in `/api/state` as `saveError`.

## The web UI

A single HTML page embedded in the payload, served on port 8089, with no external resources. The page polls `/api/state` for the raw stick values, the applied settings and the per-side status. Commands are `POST /api/apply`, `/api/disable` and `/api/close`, and must carry the `X-Deadzone-Request: 1` header, which the page sets. Requests must use the console IP directly. There is no authentication: anyone on the LAN can control the payload.
