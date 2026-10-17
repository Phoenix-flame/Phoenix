Blaster model for the Robot Arena showcase: blaster-c from Kenney's "Blaster Kit"
(www.kenney.nl, CC0 public domain, see License.txt).

Textures/colormap.png is stored flipped top-to-bottom compared with Kenney's
original. The engine's model loader flips UVs (aiProcess_FlipUVs) and its texture
loader flips images, a pair that suits the backpack model but samples standard
OBJ palettes upside down. Flipping the palette once here cancels that out.
