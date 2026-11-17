# Third-party patches (vendored raylib 6.2)

Wildlands vendors raylib under `third_party/raylib`. The following minimal,
documented patches are applied to its bundled libraries. None of them change
behavior for well-formed input; they close undefined behavior on malformed
input and silence legitimate compiler diagnostics.

## `src/external/m3d.h` — `_m3d_getidx` default case

**Diagnostic:** `external/m3d.h:3796: warning: 'mi' may be used
uninitialized [-Wmaybe-uninitialized]` (in `m3d_load`, voxel chunk decoder).

**Analysis.** `_m3d_getidx(data, type, idx)` wrote `*idx` only for
`type == 1, 2, 4`. Index sizes come from the file header as
`1 << (types >> n & 3)`, i.e. `{1, 2, 4, 8}`; the voxel decoder validates
`vp_s <= 2` before use, so current callers only reach valid cases — but the
helper's contract silently skipped the write for any other value, leaving the
caller's variable uninitialized (exactly what GCC reports at the
`model->voxel[i].data[j++] = (M3D_VOXEL)mi;` reads). Any future call site
with a missing size validation would turn that into a real
uninitialized-value bug.

**Patch.** A `default: *idx = 0;` case: an index whose encoded size is
invalid decodes as 0 instead of leaving the destination untouched. Every
execution path now writes `*idx` before it is read.

## `src/external/par_shapes.h` — `par_shapes_create_disk` input guard

**Diagnostic:** `raylib.h:132:33: warning: argument 1 range [...] exceeds
maximum object size [...] [-Walloc-size-larger-than=]` (GCC 14, building
`rmodels.c`).

**Analysis.** `par_shapes_create_disk` copied `slices + 1` into
`mesh->npoints` and multiplied it into malloc sizes without validating
`slices`. A negative slice count produced negative counts (the diagnostic's
`-6 .. -275200` range) that become huge `size_t` allocations.

**Patch.** `if (slices < 3) return 0;` — a disk needs at least three
slices; callers passing sane values (raylib's `GenMeshPoly` enforces
`sides >= 3`) are unaffected.

Both patches are marked with `/* Wildlands patch: ... */` comments at the
site. To re-apply after a raylib upgrade, see this file.
