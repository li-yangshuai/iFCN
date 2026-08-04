# OGDF fixed-layer crossing orderer

This adapter uses OGDF 2025.10 (`foxglove-202510`) and its
`SugiyamaLayout` with a caller-supplied rank for every circuit node. It uses
eight deterministic barycenter starts plus OGDF's adjacent transpose pass.
Only the
within-layer order is returned to iFCN; placement, routing, compaction, port
constraints, and 2DDWave legality checks remain in the existing flow.

Build with an existing OGDF checkout:

```sh
cmake -S include/gcn_rl_layout/ogdf -B build-ogdf \
  -DOGDF_SOURCE_DIR=/path/to/ogdf -DCMAKE_BUILD_TYPE=Release
cmake --build build-ogdf -j --target ifcn_ogdf_layer_order
```

If `OGDF_SOURCE_DIR` is omitted, CMake fetches the pinned official release.
The Python flow finds `build-ogdf/ifcn_ogdf_layer_order` automatically, or the
path can be supplied with `IFCN_OGDF_ORDERER`.

It can also be enabled from the main iFCN configuration with
`-DIFCN_BUILD_OGDF_ORDERER=ON`.

## License boundary

OGDF is GPL-2.0-or-later / GPL-3.0-or-later, not MIT. The adapter is therefore
built as a separate process and is not linked into the MIT-licensed iFCN GUI or
Python extension. Any distributed `ifcn_ogdf_layer_order` binary must comply
with OGDF's GPL terms and include the corresponding OGDF source and license
notices. The main iFCN binaries can still be built and used without this
optional adapter.
