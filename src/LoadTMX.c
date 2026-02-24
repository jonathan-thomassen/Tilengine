#include "LoadTMX.h"
#include "Layer.h"
#include "LoadFile.h"
#include "Tilengine.h"
#include "simplexml.h"
#include <stdlib.h>
#include <string.h>

static TMXInfo tmxinfo;

static void init_current_layer(TLN_LayerType type) {
  TMXLayer *layer = &tmxinfo.layers[tmxinfo.num_layers];
  memset(layer, 0, sizeof(TMXLayer));
  layer->type = type;
  layer->visible = true;
  layer->parallaxx = layer->parallaxy = 1.0f;
}

static void handle_map_attribute(const char *szAttribute, int intvalue,
                                 const char *szValue) {
  if (!strcasecmp(szAttribute, "width"))
    tmxinfo.width = intvalue;
  else if (!strcasecmp(szAttribute, "height"))
    tmxinfo.height = intvalue;
  else if (!strcasecmp(szAttribute, "tilewidth"))
    tmxinfo.tilewidth = intvalue;
  else if (!strcasecmp(szAttribute, "tileheight"))
    tmxinfo.tileheight = intvalue;
  else if (!strcasecmp(szAttribute, "backgroundcolor")) {
    sscanf(&szValue[1], "%x", &tmxinfo.bgcolor);
    tmxinfo.bgcolor += 0xFF000000;
  }
}

static void handle_tileset_attribute(const char *szAttribute, int intvalue,
                                     const char *szValue) {
  TMXTileset *tileset = &tmxinfo.tilesets[tmxinfo.num_tilesets];
  if (!strcasecmp(szAttribute, "firstgid"))
    tileset->firstgid = intvalue;
  else if (!strcasecmp(szAttribute, "source"))
    strncpy(tileset->source, szValue, sizeof(tileset->source));
}

static void handle_layer_attribute(const char *szAttribute, int intvalue,
                                   float floatvalue, const char *szValue) {
  TMXLayer *layer = &tmxinfo.layers[tmxinfo.num_layers];
  if (!strcasecmp(szAttribute, "name"))
    strncpy(layer->name, szValue, sizeof(layer->name));
  else if (!strcasecmp(szAttribute, "id"))
    layer->id = intvalue;
  else if (!strcasecmp(szAttribute, "visible"))
    layer->visible = (bool)intvalue;
  else if (!strcasecmp(szAttribute, "width"))
    layer->width = intvalue;
  else if (!strcasecmp(szAttribute, "height"))
    layer->height = intvalue;
  else if (!strcasecmp(szAttribute, "parallaxx"))
    layer->parallaxx = floatvalue;
  else if (!strcasecmp(szAttribute, "parallaxy"))
    layer->parallaxy = floatvalue;
  else if (!strcasecmp(szAttribute, "offsetx"))
    layer->offsetx = floatvalue;
  else if (!strcasecmp(szAttribute, "offsety"))
    layer->offsety = floatvalue;
  else if (!strcasecmp(szAttribute, "opacity"))
    layer->opacity = floatvalue;
  else if (!strcasecmp(szAttribute, "tintcolor"))
    sscanf(&szValue[1], "%x", &layer->tintcolor);
}

static void handle_image_attribute(const char *szAttribute, int intvalue,
                                   const char *szValue) {
  TMXLayer *layer = &tmxinfo.layers[tmxinfo.num_layers];
  if (!strcasecmp(szAttribute, "source"))
    strncpy(layer->image, szValue, sizeof(layer->name));
  else if (!strcasecmp(szAttribute, "width"))
    layer->width = intvalue;
  else if (!strcasecmp(szAttribute, "height"))
    layer->height = intvalue;
}

static bool is_layer_tag(const char *szName) {
  return !strcasecmp(szName, "layer") || !strcasecmp(szName, "objectgroup") ||
         !strcasecmp(szName, "imagelayer");
}

static void handle_add_attribute(const char *szName, const char *szAttribute,
                                 int intvalue, float floatvalue,
                                 const char *szValue) {
  if (!strcasecmp(szName, "map"))
    handle_map_attribute(szAttribute, intvalue, szValue);
  else if (!strcasecmp(szName, "tileset"))
    handle_tileset_attribute(szAttribute, intvalue, szValue);
  else if (is_layer_tag(szName))
    handle_layer_attribute(szAttribute, intvalue, floatvalue, szValue);
  else if (!strcasecmp(szName, "image"))
    handle_image_attribute(szAttribute, intvalue, szValue);
}

static void handle_finish_tag(const char *szName) {
  bool is_layer = is_layer_tag(szName);
  if (!strcasecmp(szName, "tileset") &&
      tmxinfo.num_tilesets < TMX_MAX_TILESET - 1)
    tmxinfo.num_tilesets += 1;
  else if (is_layer && tmxinfo.num_layers < TMX_MAX_LAYER - 1)
    tmxinfo.num_layers += 1;
  else if (!strcasecmp(szName, "object"))
    tmxinfo.layers[tmxinfo.num_layers].num_objects += 1;
}

/* XML parser callback */
static void *handler(SimpleXmlParser /*parser*/, SimpleXmlEvent evt,
                     const char *szName, const char *szAttribute,
                     const char *szValue) {
  switch (evt) {
  case ADD_SUBTAG:
    if (!strcasecmp(szName, "layer"))
      init_current_layer(LAYER_TILE);
    else if (!strcasecmp(szName, "objectgroup"))
      init_current_layer(LAYER_OBJECT);
    else if (!strcasecmp(szName, "imagelayer"))
      init_current_layer(LAYER_BITMAP);
    else if (!strcasecmp(szName, "tileset")) {
      TMXTileset *tileset = &tmxinfo.tilesets[tmxinfo.num_tilesets];
      memset(tileset, 0, sizeof(TMXTileset));
    }
    break;
  case ADD_ATTRIBUTE:
    handle_add_attribute(szName, szAttribute, atoi(szValue),
                         (float)atof(szValue), szValue);
    break;
  case FINISH_TAG:
    handle_finish_tag(szName);
    break;
  default:
    break;
  }
  return &handler;
}

static int compare(void const *d1, void const *d2) {
  TMXTileset const *t1 = d1;
  TMXTileset const *t2 = d2;
  return t1->firstgid > t2->firstgid;
}

/* loads common info about a .tmx file */
bool TMXLoad(const char *filename, TMXInfo *info) {
  SimpleXmlParser parser;
  ssize_t size;
  uint8_t *data;
  bool retval = false;

  /* already cached: return as is */
  if (!strcasecmp(filename, tmxinfo.filename)) {
    memcpy(info, &tmxinfo, sizeof(TMXInfo));
    return true;
  }

  /* load file */
  data = (uint8_t *)LoadFile(filename, &size);
  if (!data) {
    if (size == 0)
      TLN_SetLastError(TLN_ERR_FILE_NOT_FOUND);
    else if (size == -1)
      TLN_SetLastError(TLN_ERR_OUT_OF_MEMORY);
    return retval;
  }

  /* parse */
  memset(&tmxinfo, 0, sizeof(TMXInfo));
  parser = simpleXmlCreateParser((char *)data, (long)size);
  if (parser != NULL) {
    if (simpleXmlParse(parser, handler) != 0) {
      printf("parse error on line %li:\n%s\n", simpleXmlGetLineNumber(parser),
             simpleXmlGetErrorDescription(parser));
    } else {
      strncpy(tmxinfo.filename, filename, sizeof(tmxinfo.filename));
      TLN_SetLastError(TLN_ERR_OK);
      retval = true;
    }
  } else
    TLN_SetLastError(TLN_ERR_OUT_OF_MEMORY);

  /* sort tilesets by gid */
  qsort(&tmxinfo.tilesets, tmxinfo.num_tilesets, sizeof(TMXTileset), compare);

  simpleXmlDestroyParser(parser);
  free(data);
  if (retval)
    memcpy(info, &tmxinfo, sizeof(TMXInfo));
  return retval;
}

/* returns index of suitable tileset acoording to gid range, -1 if not valid
 * tileset found */
int TMXGetSuitableTileset(const TMXInfo *info, int gid, TLN_Tileset *tilesets) {
  for (int c = 0; c < info->num_tilesets; c += 1) {
    if (tilesets[c] == NULL)
      continue;
    const int first = info->tilesets[c].firstgid;
    if (gid >= first && gid < first + tilesets[c]->numtiles)
      return c;
  }
  return -1;
}

/*returns first layer of requested type */
TMXLayer *TMXGetFirstLayer(TMXInfo *info, TLN_LayerType type) {
  for (int c = 0; c < info->num_layers; c += 1) {
    if (info->layers[c].type == type)
      return &info->layers[c];
  }
  return NULL;
}

/* returns specified layer */
TMXLayer *TMXGetLayer(TMXInfo *info, const char *name) {
  for (int c = 0; c < info->num_layers; c += 1) {
    if (!strcasecmp(info->layers[c].name, name))
      return &info->layers[c];
  }
  return NULL;
}