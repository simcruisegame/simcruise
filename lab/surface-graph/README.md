# The knowledge graph chooses the surfaces

Prototype for docs/research/world-detail-methods.md, section "The knowledge graph chooses the surfaces".

    python lab/surface-graph/resolve.py 18180 27907 --month 11 --out Saved/SurfaceGraph

It reads the tile (`data/tiles`), the graph's rules (`data/knowledge/place-graph.json`) and the surface rules here (`surface-rules.json`), resolves a `surface:*` material for each road and land patch, and writes `surface-plan-X-Y.json` (choice, confidence, reasons, rule colour and measured colour) and a picture: unbaked aerial | graph choice | choice corrected toward the measured colour. Needs NumPy and Pillow.
