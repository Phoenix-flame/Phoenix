Dungeon props for the Fire showcase (File > Showcases > Fire) and Create > Fire: torches,
candles, walls, floor tiles, a column and furniture from "KayKit : Dungeon Remastered (1.0)" by Kay
Lousberg (www.kaylousberg.com), CC0 public domain, see License.txt.

Source: https://github.com/KayKit-Game-Assets/KayKit-Dungeon-Remastered-1.0
        (addons/kaykit_dungeon_remastered/Assets/gltf)

Each .glb was split into <name>.gltf + <name>.bin with its embedded texture replaced by
a reference to the shared dungeon_texture.png (all models embed the same atlas), so the
engine's loader, which reads textures from files, finds it. Geometry is unchanged.

Also, from Kenney (www.kenney.nl, CC0, see License_Kenney.txt), taken from the mirror at
https://github.com/shorepine/kenney:
  fire_basket   <- Graveyard Kit, 3d/graveyard/fire-basket.glb (the brazier's bowl)
  campfire_pit  <- Survival Kit, 3d/survival/campfire-pit.glb
with their kits' palettes as kenney_graveyard_colormap.png / kenney_survival_colormap.png.

Sizes are in KayKit units: a wall piece is 4 x 4, a large floor tile 4 x 4 (the showcase
scales the room down to metres).
