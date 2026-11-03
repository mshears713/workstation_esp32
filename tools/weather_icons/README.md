# Weather icon generation

The base generator `gen_icons.py` produces all eight existing semantic keys.
Run `gen_pixel_icons.py` afterward to replace six with the pixel-art variants;
fog and snow remain the base icons. Both require Python and Pillow.
The scripts write `main/weather_icons.c`. The saved PNGs are visual references;
the C arrays are generated from the manually specified pixel shapes in code.
The semantic lookup and the backend day/night mapping must remain unchanged.
