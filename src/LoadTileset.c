/*
 * Tilengine - The 2D retro graphics engine with raster effects
 * Copyright (C) 2015-2019 Marc Palacios Domenech <mailto:megamarc@hotmail.com>
 * All rights reserved
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * */

#include "LoadFile.h"
#include "Tilengine.h"
#include "Tileset.h"
#include "simplexml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* properties */
typedef enum {
  PROPERTY_NONE,
  PROPERTY_TYPE,
  PROPERTY_PRIORITY,
} Property;

/* image context */
typedef enum {
  CONTEXT_NONE,
  CONTEXT_TILESET,
  CONTEXT_TILE,
} ImageContext;

/* load manager */
struct {
  char source[64];
  int tilecount;
  int tilewidth;
  int tileheight;
  int spacing;
  int margin;
  ImageContext context;
  TLN_TileAttributes *attributes; /* array of attributes */
  TLN_SequencePack sp;
  TLN_SequenceFrame frames[100];
  TLN_TileImage *images; /* array of images */
  TLN_TileImage *image;  /* current image */
  int frame_count;

  /* tile-specific values */
  struct {
    int id;            /* id of tile */
    int type;          /* type of tile */
    Property property; /* property being read */
    bool priority;     /* value of priority property */
    TLN_Bitmap bitmap; /* bitmap of image-based tile */
  } tile;
} static loader;

static void handle_subtag(const char *szName) {
  if (!strcasecmp(szName, "animation"))
    loader.frame_count = 0;
  else if (!strcasecmp(szName, "tileset"))
    loader.context = CONTEXT_TILESET;
  else if (!strcasecmp(szName, "tile"))
    loader.context = CONTEXT_TILE;
}

static void handle_tileset_attribute(const char *szAttribute,
                                     const char *szValue) {
  if (!strcasecmp(szAttribute, "tilewidth"))
    loader.tilewidth = atoi(szValue);
  else if (!strcasecmp(szAttribute, "tileheight"))
    loader.tileheight = atoi(szValue);
  else if (!strcasecmp(szAttribute, "margin"))
    loader.margin = atoi(szValue);
  else if (!strcasecmp(szAttribute, "spacing"))
    loader.spacing = atoi(szValue);
  else if (!strcasecmp(szAttribute, "tilecount"))
    loader.tilecount = atoi(szValue);
}

static void handle_image_attribute(const char *szAttribute,
                                   const char *szValue) {
  if (strcasecmp(szAttribute, "source") != 0)
    return;
  strncpy(loader.source, szValue, sizeof(loader.source));
  loader.source[sizeof(loader.source) - 1] = '\0';
  if (loader.context == CONTEXT_TILE) {
    loader.tile.bitmap = TLN_LoadBitmap(loader.source);
    loader.source[0] = 0;
  }
}

static void handle_property_name(const char *szValue) {
  if (!strcasecmp(szValue, "type"))
    loader.tile.property = PROPERTY_TYPE;
  else if (!strcasecmp(szValue, "priority"))
    loader.tile.property = PROPERTY_PRIORITY;
  else
    loader.tile.property = PROPERTY_NONE;
}

static void handle_property_value(const char *szValue) {
  if (loader.tilecount == 0)
    return;
  if (loader.tile.property == PROPERTY_TYPE)
    loader.attributes[loader.tile.id].type = (uint8_t)atoi(szValue);
  else if (loader.tile.property == PROPERTY_PRIORITY)
    loader.attributes[loader.tile.id].priority = !strcasecmp(szValue, "true");
}

static void handle_property_attribute(const char *szAttribute,
                                      const char *szValue) {
  if (!strcasecmp(szAttribute, "name"))
    handle_property_name(szValue);
  else if (!strcasecmp(szAttribute, "value"))
    handle_property_value(szValue);
}

static void handle_add_attribute(const char *szName, const char *szAttribute,
                                 const char *szValue) {
  if (!strcasecmp(szName, "tileset"))
    handle_tileset_attribute(szAttribute, szValue);
  else if (!strcasecmp(szName, "image"))
    handle_image_attribute(szAttribute, szValue);
  else if (!strcasecmp(szName, "tile")) {
    if (!strcasecmp(szAttribute, "id"))
      loader.tile.id = atoi(szValue);
    else if (!strcasecmp(szAttribute, "type"))
      loader.tile.type = atoi(szValue);
  } else if (!strcasecmp(szName, "property"))
    handle_property_attribute(szAttribute, szValue);
  else if (!strcasecmp(szName, "frame")) {
    if (!strcasecmp(szAttribute, "tileid"))
      loader.frames[loader.frame_count].index = atoi(szValue) + 1;
    else if (!strcasecmp(szAttribute, "duration"))
      loader.frames[loader.frame_count].delay = atoi(szValue) * 60 / 1000;
  }
}

static void handle_finish_attributes(const char *szName) {
  if (strcasecmp(szName, "tileset") != 0 || loader.tilecount == 0)
    return;
  loader.attributes = (TLN_TileAttributes *)calloc(loader.tilecount,
                                                   sizeof(TLN_TileAttributes));
  loader.images =
      (TLN_TileImage *)calloc(loader.tilecount, sizeof(TLN_TileImage));
  loader.image = loader.images;
}

static void handle_finish_tile(void) {
  if (loader.tilecount == 0)
    return;
  if (loader.context == CONTEXT_TILESET) {
    TLN_TileAttributes *attribute = &loader.attributes[loader.tile.id];
    attribute->priority = loader.tile.priority;
    attribute->type = (uint8_t)loader.tile.type;
  } else if (loader.context == CONTEXT_TILE) {
    loader.image->bitmap = loader.tile.bitmap;
    loader.image->id = (uint16_t)loader.tile.id;
    loader.image->type = (uint8_t)loader.tile.type;
    loader.image += 1;
  }
}

static void handle_finish_animation(void) {
  char name[16];
  sprintf(name, "%d", loader.tile.id);
  TLN_Sequence sequence = TLN_CreateSequence(name, loader.tile.id + 1,
                                             loader.frame_count, loader.frames);
  if (loader.sp == NULL)
    loader.sp = TLN_CreateSequencePack();
  TLN_AddSequenceToPack(loader.sp, sequence);
}

static void handle_finish_tag(const char *szName) {
  if (!strcasecmp(szName, "frame"))
    loader.frame_count++;
  else if (!strcasecmp(szName, "tile"))
    handle_finish_tile();
  else if (!strcasecmp(szName, "animation"))
    handle_finish_animation();
}

/* XML parser callback */
static void *handler(SimpleXmlParser /*parser*/, SimpleXmlEvent evt,
                     const char *szName, const char *szAttribute,
                     const char *szValue) {
  switch (evt) {
  case ADD_SUBTAG:
    handle_subtag(szName);
    break;
  case ADD_ATTRIBUTE:
    handle_add_attribute(szName, szAttribute, szValue);
    break;
  case FINISH_ATTRIBUTES:
    handle_finish_attributes(szName);
    break;
  case FINISH_TAG:
    handle_finish_tag(szName);
    break;
  default:
    break;
  }
  return &handler;
}

/* cache section: keeps already loaded tilesets so it doesnt spawn multiple
 * instances of the same */
#define CACHE_SIZE 16
static int cache_entries = 0;
struct {
  char name[200];
  TLN_Tileset tileset;
} static cache[16];

static TLN_Tileset search_cache(const char *name) {
  for (int c = 0; c < cache_entries; c += 1) {
    if (!strcmp(cache[c].name, name))
      return cache[c].tileset;
  }
  return NULL;
}

static void add_to_cache(const char *name, TLN_Tileset tileset) {
  if (cache_entries < CACHE_SIZE - 1) {
    strncpy(cache[cache_entries].name, name, sizeof(cache[0].name));
    cache[cache_entries].tileset = tileset;
    cache_entries += 1;
  }
}

static TLN_Tileset load_tile_based_tileset(const char *filename) {
  FileInfo fi = {0};
  char imagepath[200];

  SplitFilename(filename, &fi);
  if (fi.path[0] != 0)
    snprintf(imagepath, sizeof(imagepath), "%s/%s", fi.path, loader.source);
  else
    strncpy(imagepath, loader.source, sizeof(imagepath));

  TLN_Bitmap bitmap = TLN_LoadBitmap(imagepath);
  if (!bitmap) {
    TLN_SetLastError(TLN_ERR_FILE_NOT_FOUND);
    return NULL;
  }

  int dx = loader.tilewidth + loader.spacing;
  int dy = loader.tileheight + loader.spacing;
  int htiles =
      (TLN_GetBitmapWidth(bitmap) - loader.margin * 2 + loader.spacing) / dx;
  int vtiles =
      (TLN_GetBitmapHeight(bitmap) - loader.margin * 2 + loader.spacing) / dy;
  int tilecount = loader.tilecount != 0 ? loader.tilecount : htiles * vtiles;

  TLN_Tileset tileset =
      TLN_CreateTileset(tilecount, loader.tilewidth, loader.tileheight,
                        TLN_ClonePalette(TLN_GetBitmapPalette(bitmap)),
                        loader.sp, loader.attributes);
  if (tileset == NULL) {
    TLN_SetLastError(TLN_ERR_OUT_OF_MEMORY);
    TLN_DeleteBitmap(bitmap);
    return NULL;
  }

  int pitch = TLN_GetBitmapPitch(bitmap);
  for (int id = 0, y = 0; y < vtiles; y++) {
    for (int x = 0; x < htiles; x++, id++) {
      uint8_t const *srcptr = TLN_GetBitmapPtr(bitmap, loader.margin + x * dx,
                                               loader.margin + y * dy);
      if (id < tilecount)
        TLN_SetTilesetPixels(tileset, id, srcptr, pitch);
    }
  }
  tileset->tiles_per_row = htiles;
  TLN_DeleteBitmap(bitmap);
  return tileset;
}

static TLN_Tileset load_image_based_tileset(void) {
  TLN_Tileset tileset = TLN_CreateImageTileset(loader.tilecount, loader.images);
  if (tileset == NULL)
    TLN_SetLastError(TLN_ERR_OUT_OF_MEMORY);
  return tileset;
}

/*!
 * \brief
 * Loads a tileset from a Tiled .tsx file
 *
 * \param filename
 * TSX file to load
 *
 * \returns
 * Reference to the newly loaded tileset or NULL if error
 *
 * \remarks
 * An associated palette is also created, it can be obtained calling
 * TLN_GetTilesetPalette()
 */
TLN_Tileset TLN_LoadTileset(const char *filename) {
  TLN_Tileset tileset = search_cache(filename);
  if (tileset)
    return tileset;

  ssize_t size = 0;
  uint8_t *data = (uint8_t *)LoadFile(filename, &size);
  if (!data) {
    TLN_SetLastError(size == 0 ? TLN_ERR_FILE_NOT_FOUND
                               : TLN_ERR_OUT_OF_MEMORY);
    return NULL;
  }

  memset(&loader, 0, sizeof(loader));
  SimpleXmlParser parser = simpleXmlCreateParser((char *)data, (long)size);
  if (parser != NULL) {
    if (simpleXmlParse(parser, handler) != 0) {
      printf("parse error on line %li:\n%s\n", simpleXmlGetLineNumber(parser),
             simpleXmlGetErrorDescription(parser));
      simpleXmlDestroyParser(parser);
      free(data);
      TLN_SetLastError(TLN_ERR_WRONG_FORMAT);
      return NULL;
    }
  } else
    TLN_SetLastError(TLN_ERR_OUT_OF_MEMORY);

  simpleXmlDestroyParser(parser);
  free(data);

  tileset = loader.source[0] != 0 ? load_tile_based_tileset(filename)
                                  : load_image_based_tileset();

  free(loader.attributes);
  free(loader.images);

  if (tileset != NULL) {
    add_to_cache(filename, tileset);
    TLN_SetLastError(TLN_ERR_OK);
  }
  return tileset;
}
