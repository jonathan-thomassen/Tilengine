#ifndef PALETTELAYER_H
#define PALETTELAYER_H

#include "Tilengine.h"

/* Load a combined palette text file and split it into 8 sub-palettes.
 * The file contains one #RRGGBB hex color per line (blank / non-'#' lines
 * skipped).  Colors are read sequentially; sub-palette[i] gets 'stride'
 * consecutive colors starting at offset i*stride.
 * Each out[i] is a newly created TLN_Palette(stride); caller must free. */
void load_and_split_palette(const char *path, int stride, TLN_Palette out[8]);

/* Parse the "Palette" objectgroup from tmxpath and apply palette indices
 * directly to the tilemap tiles so TLN_SetGlobalPalette drives per-tile color.
 */
void apply_palette_layer(TLN_Tilemap tilemap, const char *tmxpath);

#endif /* PALETTELAYER_H */
