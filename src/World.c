#include "Engine.h"
#include "Layer.h"
#include "LoadTMX.h"
#include "Palette.h"
#include "Sprite.h"
#include "Tilengine.h"
#include <stdio.h>
#define WDB(msg, ...)                                                          \
  do {                                                                         \
    printf("[WLD] " msg "\n", ##__VA_ARGS__);                                  \
    fflush(stdout);                                                            \
  } while (0)

#define MAX_TMX_ITEM 100

/* info for current world */
static TMXInfo tmxinfo;
static int first;

/*!
 * \brief Loads and assigns complete TMX file
 * \param filename TMX file to load
 * \param first_layer Starting layer number where place the loaded tmx
 */
bool TLN_LoadWorld(const char *filename, int first_layer) {
  WDB("TMXLoad start");
  if (!TMXLoad(filename, &tmxinfo))
    return NULL;
  WDB("TMXLoad ok, num_layers=%d num_tilesets=%d", tmxinfo.num_layers,
      tmxinfo.num_tilesets);

  if (tmxinfo.num_layers > MAX_TMX_ITEM)
    tmxinfo.num_layers = MAX_TMX_ITEM;

  /* load and assign each layer type */
  first = first_layer;
  for (int c = 0; c < tmxinfo.num_layers; c += 1) {
    TMXLayer const *tmxlayer = &tmxinfo.layers[c];
    const int layerindex = tmxinfo.num_layers - c - 1 + first;
    WDB("layer %d type=%d name=%s layerindex=%d", c, tmxlayer->type,
        tmxlayer->name, layerindex);
    switch (tmxlayer->type) {
    case LAYER_NONE:
      break;

    case LAYER_TILE: {
      TLN_Tilemap tilemap = TLN_LoadTilemap(filename, tmxlayer->name);
      WDB("  TLN_LoadTilemap -> %p", (void *)tilemap);
      TLN_SetLayerTilemap(layerindex, tilemap);
      WDB("  SetLayerTilemap done");
    } break;

    case LAYER_OBJECT: {
      TLN_ObjectList objectlist = TLN_LoadObjectList(filename, tmxlayer->name);
      WDB("  TLN_LoadObjectList -> %p", (void *)objectlist);
      TLN_SetLayerObjects(layerindex, objectlist, NULL);
      WDB("  SetLayerObjects done");
    } break;

    case LAYER_BITMAP: {
      TLN_Bitmap bitmap = TLN_LoadBitmap(tmxlayer->image);
      WDB("  TLN_LoadBitmap -> %p", (void *)bitmap);
      TLN_SetLayerBitmap(layerindex, bitmap);
      WDB("  SetLayerBitmap done");
    } break;
    }

    /* direct set of layer properties */
    Layer *layer = GetLayer(layerindex);
    layer->world.xfactor = tmxlayer->parallaxx;
    layer->world.yfactor = tmxlayer->parallaxy;
    layer->world.offsetx = (int)tmxlayer->offsetx;
    layer->world.offsety = (int)tmxlayer->offsety;

    /* opacity selects blend mode */
    if (tmxlayer->visible) {
      int opacity = (int)(tmxlayer->opacity * 100);
      if (opacity > 87)
        TLN_SetLayerBlendMode(layerindex, BLEND_NONE, 0);
      else if (opacity > 62)
        TLN_SetLayerBlendMode(layerindex, BLEND_MIX75, 0);
      else if (opacity > 37)
        TLN_SetLayerBlendMode(layerindex, BLEND_MIX50, 0);
      else if (opacity > 12)
        TLN_SetLayerBlendMode(layerindex, BLEND_MIX25, 0);
    } else
      TLN_DisableLayer(layerindex);
    WDB("layer %d done", c);
  }
  WDB("all layers done");

  /* sets background color if defined */
  if (tmxinfo.bgcolor != 0) {
    Color bgcolor;
    bgcolor.value = tmxinfo.bgcolor;
    TLN_SetBGColor(bgcolor.r, bgcolor.g, bgcolor.b);
  } else
    TLN_DisableBGColor();
  return true;
}

/*!
 * \brief Releases world resources loaded with TLN_LoadWorld
 */
void TLN_ReleaseWorld(void) {

  for (int c = 0; c < tmxinfo.num_layers; c += 1) {
    TMXLayer const *tmxlayer = &tmxinfo.layers[c];
    const int layerindex = tmxinfo.num_layers - c - 1 + first;

    Layer *layer = GetLayer(layerindex);
    layer->flags.ok = false;
    switch (tmxlayer->type) {
    case LAYER_NONE:
      break;

    case LAYER_TILE:
      TLN_DeleteTilemap(layer->tilemap);
      break;

    case LAYER_OBJECT:
      TLN_DeleteObjectList(layer->objects);
      break;

    case LAYER_BITMAP:
      TLN_DeleteBitmap(layer->bitmap);
      break;
    }
  }
}

/*!
 * \brief Sets layer parallax factor to use in conjunction with \ref
 * TLN_SetWorldPosition
 * \param nlayer Layer index [0, num_layers - 1]
 * \param x Horizontal parallax factor
 * \param y Vertical parallax factor
 */
bool TLN_SetLayerParallaxFactor(int nlayer, float x, float y) {
  Layer *layer;
  if (nlayer >= engine->numlayers) {
    TLN_SetLastError(TLN_ERR_IDX_LAYER);
    return false;
  }

  layer = &engine->layers[nlayer];
  layer->world.xfactor = x;
  layer->world.yfactor = y;
  layer->flags.dirty = true;
  TLN_SetLastError(TLN_ERR_OK);
  return true;
}

/*!
 * \brief Sets global world position, moving all layers in sync according to
 * their parallax factor
 * \param x horizontal position in world space
 * \param y vertical position in world space
 */
void TLN_SetWorldPosition(int x, int y) {
  engine->world.x = x;
  engine->world.y = y;
  engine->world.dirty = true;
}

/*!
 * \brief Sets the sprite position in world space coordinates
 * \param nsprite Id of the sprite [0, num_sprites - 1]
 * \param x Horizontal world position of pivot (0 = left margin)
 * \param y Vertical world position of pivot (0 = top margin)
 * \sa TLN_SetSpritePivot
 */
bool TLN_SetSpriteWorldPosition(int nsprite, int x, int y) {
  Sprite *sprite;
  if (nsprite >= engine->numsprites) {
    TLN_SetLastError(TLN_ERR_IDX_SPRITE);
    return false;
  }

  sprite = &engine->sprites[nsprite];
  sprite->world_pos.x = x;
  sprite->world_pos.y = y;
  SetSpriteFlag(sprite, SPRITE_FLAG_WORLD_SPACE, true);
  SetSpriteFlag(sprite, SPRITE_FLAG_DIRTY, true);

  TLN_SetLastError(TLN_ERR_OK);
  return true;
}
