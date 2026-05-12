# vxl-render experiment

This directory contains a standalone rendering experiment that tries to stay close to
the reference project's `vpl_renderer` hardware path.

## Assets

The experiment now reads the centralized project assets tree under the repository root:

- `assets/vehicles/rhino_tank/`
  - `htnk.vxl`
  - `htnk.hva`
  - `htnktur.vxl`
  - `htnktur.hva`
  - `htnkbarl.vxl`
  - `htnkbarl.hva`
- `assets/palettes/voxel/voxels.vpl`
- `assets/palettes/theater/unittem.pal`

## Build target

The root `CMakeLists.txt` exposes a standalone executable target:

- `vxl_render_experiment`

## What the experiment does

- loads VXL voxel data for body, turret and barrel
- loads HVA transforms for each part
- loads the VPL lighting lookup table
- loads the external palette (`unittem.pal`)
- uploads voxels as instanced cube data to OpenGL
- applies a shader that mirrors the reference `box_vmain/box_pmain` logic:
  - `translation_to_center * scale * base * offset * world`
  - VXL projection
  - normal-based VPL lookup
  - palette lookup
  - remap for palette indices `16..31`
  - `extra_light`

## Runtime controls

- `Ctrl + D`: show or hide the control panel
- `Scale`: mirrors `scale_factor`
- `Extra light`: mirrors `extra_light`
- `Light direction`: mirrors `light_dir`
- `Z angle`, `XY angle`, `Rotation theta`, `Rotation phi`: mirror the viewer world state
- `Turret rotation`, `Turret offset`: mirror runtime turret assembly parameters
- `Remap color`: tests the remap stage behavior

## Notes

- This experiment intentionally does not draw the RTS map. It renders the voxel model
  directly on a blue background so it is easier to compare against the reference viewer.
- The implementation reuses the main repo VXL/VPL parsing code, but keeps the runtime
  renderer isolated from the main demo.
