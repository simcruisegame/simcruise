# Grass field prototype

Driver's-eye render of a roadside lawn: the game's current method, the grass field, and the field when dormant. NumPy and Pillow.

    python lab/grass-field/field.py data/art/ground/grass-detail.jpg   # -> A-now.png B-field.png C-field-dormant.png

The game version is the GrassField branch of the ground shader (SimCruiseWorldResources.cpp). Note: this prototype offsets the blade shells away from the camera; the shader uses the correct direction (toward the camera).
