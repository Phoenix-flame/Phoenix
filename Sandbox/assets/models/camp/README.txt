Camp props for the Night Camp showcase (File > Showcases > Night Camp) and Create > Fire >
Lantern, from Kenney (www.kenney.nl), CC0 public domain, see License.txt. Taken from the
mirror at https://github.com/shorepine/kenney:

  lantern_candle       <- Graveyard Kit, 3d/graveyard/lantern-candle.glb
  tent_canvas, tree, tree_tall, tree_log, rock_a, rock_b, bedroll, box, bucket,
  barrel, signpost_single, grass_large
                       <- Survival Kit, 3d/survival/<same name with dashes>.glb

Each .glb was split into <name>.gltf + <name>.bin with its texture pointing at the kit's
palette, kenney_graveyard_colormap.png or kenney_survival_colormap.png (the kits' own
Textures/colormap.png), so the engine's loader, which reads textures from files, finds
it. Geometry is unchanged. The campfire, torch and candle come from ../dungeon.
