# The AI models' weights were trained on data released for research or non-commercial use
# only (Cityscapes; DIV2K, Flickr2K, OST), so they are development tools and never shipped
# (docs/data-sources.md); the game runs without them. Every data/ai/*.onnx is left out, so a
# new model is not shipped by accident; scriptselease.ps1 refuses a zip with any .onnx.
robocopy (Join-Path $Root 'data') (Join-Path $Game 'data') /MIR /XD @Skip /XF *.onnx /NFL /NDL /NJH /NJS /NP | Out-Null
