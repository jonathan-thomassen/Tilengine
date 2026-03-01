/*
 * Tilengine - The 2D retro graphics engine with raster effects
 * Copyright (C) 2015-2019 Marc Palacios Domenech <mailto:megamarc@hotmail.com>
 * All rights reserved
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * */

#include "ObjectList.h"
#include "Engine.h"
#include "LoadFile.h"
#include "LoadTMX.h"
#include "Sprite.h"
#include "Tilengine.h"
#include "simplexml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define ODB(msg, ...)                                                          \
  do {                                                                         \
    char _odb_buf[256];                                                        \
    snprintf(_odb_buf, sizeof(_odb_buf), "[OBJ] " msg, ##__VA_ARGS__);         \
    tln_trace(TLN_LOG_VERBOSE, _odb_buf);                                      \
  } while (0)

/* properties */
typedef enum {
  PROPERTY_NONE,
  PROPERTY_TYPE,
  PROPERTY_PRIORITY,
} Property;

/* load manager */
struct {
  TMXLayer *layer;
  bool state;
  TLN_ObjectList objects;
  TLN_Object object;
  Property property; /* current property */
} static loader;

static bool CloneObjectToList(TLN_ObjectList list, TLN_Object const *data);
static void resolve_object_tilesets(TMXInfo *info);
static void handle_add_attribute(const char *szName, const char *szAttribute,
                                 const char *szValue);
static void handle_finish_attributes(const char *szName);
static void handle_finish_tag(const char *szName);

static void handle_object_gid_attribute(const char *szValue) {
  Tile tile;
  tile.value = strtoul(szValue, NULL, 0);
  loader.object.has_gid = true;
  loader.object.flags = tile.flags;
  loader.object.gid = tile.index;
}

static void handle_object_attribute(const char *szAttribute,
                                    const char *szValue) {
  int intvalue = atoi(szValue);
  if (!strcasecmp(szAttribute, "id"))
    loader.object.id = (uint16_t)intvalue;
  else if (!strcasecmp(szAttribute, "gid"))
    handle_object_gid_attribute(szValue);
  else if (!strcasecmp(szAttribute, "x"))
    loader.object.x = intvalue;
  else if (!strcasecmp(szAttribute, "y"))
    loader.object.y = intvalue;
  else if (!strcasecmp(szAttribute, "width"))
    loader.object.width = intvalue;
  else if (!strcasecmp(szAttribute, "height"))
    loader.object.height = intvalue;
  else if (!strcasecmp(szAttribute, "type"))
    loader.object.type = (uint8_t)intvalue;
  else if (!strcasecmp(szAttribute, "visible"))
    loader.object.visible = (bool)intvalue;
  else if (!strcasecmp(szAttribute, "name"))
    strncpy(loader.object.name, szValue, sizeof(loader.object.name));
}

static void handle_property_attribute(const char *szAttribute,
                                      const char *szValue) {
  if (!strcasecmp(szAttribute, "name")) {
    loader.property =
        !strcasecmp(szValue, "priority") ? PROPERTY_PRIORITY : PROPERTY_NONE;
  } else if (!strcasecmp(szAttribute, "value") &&
             loader.property == PROPERTY_PRIORITY &&
             !strcasecmp(szValue, "true")) {
    loader.object.flags += FLAG_PRIORITY;
  }
}

static void handle_add_attribute(const char *szName, const char *szAttribute,
                                 const char *szValue) {
  if (!strcasecmp(szName, "objectgroup") && !strcasecmp(szAttribute, "name")) {
    loader.state = !strcasecmp(szValue, loader.layer->name);
  } else if (!strcasecmp(szName, "object"))
    handle_object_attribute(szAttribute, szValue);
  else if (!strcasecmp(szName, "property"))
    handle_property_attribute(szAttribute, szValue);
}

static void handle_finish_attributes(const char *szName) {
  if (loader.state && !strcasecmp(szName, "objectgroup")) {
    loader.objects = TLN_CreateObjectList();
    loader.objects->id = loader.layer->id;
    loader.objects->visible = loader.layer->visible;
  }
}

static void handle_finish_tag(const char *szName) {
  if (!loader.state)
    return;
  if (!strcasecmp(szName, "objectgroup")) {
    loader.state = false;
  } else if (!strcasecmp(szName, "object")) {
    if (loader.object.has_gid)
      loader.object.y -= loader.object.height;
    CloneObjectToList(loader.objects, &loader.object);
  }
}

/* XML parser callback */
static void *handler(SimpleXmlParser /*parser*/, SimpleXmlEvent evt,
                     const char *szName, const char *szAttribute,
                     const char *szValue) {
  ODB("handler evt=%d szName=%s szAttr=%s szVal=%s", evt,
      szName ? szName : "(null)", szAttribute ? szAttribute : "(null)",
      szValue ? szValue : "(null)");
  switch (evt) {
  case ADD_SUBTAG:
    if (!strcasecmp(szName, "object")) {
      memset(&loader.object, 0, sizeof(struct _Object));
      loader.object.visible = true;
    }
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

/*!
 * \brief Creates a TLN_ObjectList
 * The list is created empty, it must be populated with TLN_AddSpriteToList()
 * and assigned to a layer with TLN_SetLayerObjects()
 * \return Reference to new object or NULL if error
 */
TLN_ObjectList TLN_CreateObjectList(void) {
  TLN_ObjectList list = NULL;
  const int size = sizeof(struct ObjectList);

  /* create */
  list = (TLN_ObjectList)CreateBaseObject(OT_OBJECTLIST, size);
  if (!list)
    return NULL;

  list->visible = true;
  TLN_SetLastError(TLN_ERR_OK);
  return list;
}

/* adds entry to linked list */
static void add_to_list(TLN_ObjectList list, struct _Object *object) {
  if (list->list == NULL)
    list->list = object;
  else
    list->last->next = object;
  list->last = object;
  list->num_items += 1;
  object->next = NULL;
}

/*!
 * \brief Adds a TLN_Object item to a given TLN_ObjectList
 * \param list Reference to the list
 * \param data Pointer to a user-provided TLN_Object. This object is internally
 * copied to the list, so it's safe to discard the user-provided one after
 * addition.
 * \return true if success or false if error
 */
static bool CloneObjectToList(TLN_ObjectList list, TLN_Object const *data) {
  struct _Object *object;

  if (!CheckBaseObject(list, OT_OBJECTLIST))
    return false;

  object = (struct _Object *)calloc(1, sizeof(struct _Object));
  if (object == NULL)
    return false;

  memcpy(object, data, sizeof(struct _Object));
  add_to_list(list, object);
  return true;
}
/*!
 * \brief Adds an image-based tileset item to given TLN_ObjectList
 *
 * \param list Reference to TLN_ObjectList
 * \param id Unique ID of the tileset object
 * \param gid Graphic Id (tile index) of the tileset object
 * \param flags Combination of FLAG_FLIPX, FLAG_FLIPY, FLAG_PRIORITY
 * \param x Layer-space horizontal coordinate of the top-left corner
 * \param y Layer-space bertical coordinate of the top-left corner
 * \return true if success or false if error
 */
bool TLN_AddTileObjectToList(TLN_ObjectList list, uint16_t /*id*/, uint16_t gid,
                             uint16_t /*flags*/, int x, int y) {
  struct _Object *object;

  if (!CheckBaseObject(list, OT_OBJECTLIST))
    return false;

  object = (struct _Object *)calloc(1, sizeof(struct _Object));
  if (object == NULL)
    return false;

  object->gid = gid;
  object->x = x;
  object->y = y;
  add_to_list(list, object);
  return true;
}

/*!
 * \brief Loads an object list from a Tiled object layer
 *
 * \param filename Name of the .tmx file containing the list
 * \param layername Name of the layer to load
 * \return Reference to the loaded object or NULL if error
 */
TLN_ObjectList TLN_LoadObjectList(const char *filename, const char *layername) {
  SimpleXmlParser parser;
  ssize_t size;
  uint8_t *data;
  TMXInfo tmxinfo = {0};

  ODB("LoadObjectList file=%s layer=%s", filename, layername);

  /* load map info */
  if (!TMXLoad(filename, &tmxinfo)) {
    TLN_SetLastError(TLN_ERR_FILE_NOT_FOUND);
    return NULL;
  }
  ODB("TMXLoad ok, num_layers=%d num_tilesets=%d", tmxinfo.num_layers,
      tmxinfo.num_tilesets);

  /* get target layer */
  memset(&loader, 0, sizeof(loader));
  if (layername)
    loader.layer = TMXGetLayer(&tmxinfo, layername);
  else
    loader.layer = TMXGetFirstLayer(&tmxinfo, LAYER_OBJECT);
  if (loader.layer == NULL) {
    TLN_SetLastError(TLN_ERR_FILE_NOT_FOUND);
    return NULL;
  }
  ODB("layer found: %s id=%d", loader.layer->name, loader.layer->id);

  /* parse */
  data = (uint8_t *)LoadFile(filename, &size);
  ODB("loaded file, size=%zd data=%p", size, (void *)data);
  parser = simpleXmlCreateParser((char *)data, (long)size);
  ODB("parser=%p, starting parse...", (void *)parser);
  if (parser != NULL) {
    if (simpleXmlParse(parser, handler) != 0) {
      printf("parse error on line %li:\n%s\n", simpleXmlGetLineNumber(parser),
             simpleXmlGetErrorDescription(parser));
      TLN_SetLastError(TLN_ERR_WRONG_FORMAT);
    } else
      TLN_SetLastError(TLN_ERR_OK);
  } else
    TLN_SetLastError(TLN_ERR_OUT_OF_MEMORY);
  ODB("parse done, objects=%p", (void *)loader.objects);

  simpleXmlDestroyParser(parser);
  free(data);

  if (loader.objects != NULL)
    resolve_object_tilesets(&tmxinfo);

  return loader.objects;
}

static void resolve_object_tilesets(TMXInfo *info) {
  struct _Object *item;
  int gid = 0;
  int c;

  /* find a gid to identify the suitable tileset */
  item = loader.objects->list;
  while (item != NULL && gid == 0) {
    if (item->gid > 0)
      gid = item->gid;
    item = item->next;
  }

  /* pure point/rect layer — no tile objects, no tileset resolution needed */
  if (gid == 0) {
    ODB("no gid objects found, skipping tileset resolution");
    return;
  }

  ODB("searching tilesets for gid=%d, num_tilesets=%d", gid,
      info->num_tilesets);

  /* load referenced tilesets */
  TLN_Tileset tilesets[TMX_MAX_TILESET] = {0};
  for (c = 0; c < info->num_tilesets; c += 1) {
    ODB("  loading tileset[%d] source='%s'", c, info->tilesets[c].source);
    tilesets[c] = TLN_LoadTileset(info->tilesets[c].source);
    ODB("  tileset[%d]=%p", c, (void *)tilesets[c]);
  }

  int suitable = TMXGetSuitableTileset(info, gid, tilesets);
  ODB("suitable=%d", suitable);
  if (suitable < 0 || suitable >= info->num_tilesets) {
    ODB("ERROR: suitable out of range! num_tilesets=%d", info->num_tilesets);
    for (c = 0; c < info->num_tilesets; c += 1)
      TLN_DeleteTileset(tilesets[c]);
    return;
  }

  TMXTileset const *tmxtileset = &info->tilesets[suitable];

  /* correct gids with firstgid offset */
  item = loader.objects->list;
  while (item != NULL) {
    if (item->gid > 0)
      item->gid = (uint16_t)(item->gid - tmxtileset->firstgid);
    item = item->next;
  }

  /* delete unused tilesets */
  for (c = 0; c < info->num_tilesets; c += 1) {
    if (c != suitable)
      TLN_DeleteTileset(tilesets[c]);
  }

  loader.objects->tileset = tilesets[suitable];
  loader.objects->width = info->width * info->tilewidth;
  loader.objects->height = info->height * info->tileheight;
}

/*!
 * \brief Creates a duplicate of a given TLN_ObjectList object
 * \param src Reference to the source object to clone
 * \return A reference to the newly cloned object list, or NULL if error
 */
TLN_ObjectList TLN_CloneObjectList(TLN_ObjectList src) {
  TLN_ObjectList list;
  struct _Object *object;

  if (!CheckBaseObject(src, OT_OBJECTLIST))
    return NULL;

  list = (TLN_ObjectList)CloneBaseObject(src);
  object = src->list;
  while (object != NULL) {
    CloneObjectToList(list, object);
    object = object->next;
  }
  list->iterator = NULL;
  return list;
}

/*!
 * \brief Returns number of items in TLN_ObjectList
 * \param list Pointer to TLN_ObjectList to query
 * \return number of items
 */
int TLN_GetListNumObjects(TLN_ObjectList list) {
  if (CheckBaseObject(list, OT_OBJECTLIST)) {
    TLN_SetLastError(TLN_ERR_OK);
    return list->num_items;
  } else {
    TLN_SetLastError(TLN_ERR_REF_LIST);
    return 0;
  }
}

/*!
 * \brief Iterates over elements in a TLN_ObjectList
 * \param list Reference to TLN_ObjectList to get items
 * \param info Pointer to user-allocated TLN_ObjectInfo struct
 * \return true if item returned, false if no more items left
 * \remarks The info pointer acts as a switch to select first/next element:
 *	* If not NULL, starts the iterator and returns the first item
 *  * If NULL, return the next item
 */
bool TLN_GetListObject(TLN_ObjectList list, TLN_ObjectInfo *info) {
  struct _Object *item;
  if (!CheckBaseObject(list, OT_OBJECTLIST)) {
    TLN_SetLastError(TLN_ERR_REF_LIST);
    return false;
  }

  /* start iterator */
  if (info != NULL) {
    list->iterator = list->list;
    list->info = info;
  }

  if (list->iterator == NULL)
    return false;

  /* copy info */
  item = list->iterator;
  info = list->info;
  info->id = item->id;
  info->gid = item->gid;
  info->flags = item->flags;
  info->x = item->x;
  info->y = item->y;
  info->width = item->width;
  info->height = item->height;
  info->type = item->type;
  info->visible = item->visible;
  if (item->name[0])
    strncpy(info->name, item->name, sizeof(info->name));

  /* advance */
  list->iterator = item->next;
  return true;
}

bool IsObjectInLine(const struct _Object *object, int x1, int x2, int y) {
  rect_t rect;
  MakeRect(&rect, object->x, object->y, object->width, object->height);
  if (y >= rect.y1 && y < rect.y2 && !(x1 > rect.x2 || x2 < rect.x1))
    return true;
  else
    return false;
}

/*!
 * \brief Deletes object list
 *
 * \param list Reference to list to delete
 * \return true if success or false if error
 */
bool TLN_DeleteObjectList(TLN_ObjectList list) {
  struct _Object *object;
  if (!CheckBaseObject(list, OT_OBJECTLIST))
    return false;

  /* delete nodes */
  object = list->list;
  while (object != NULL) {
    struct _Object *next;
    next = object->next;
    free(object);
    object = next;
  }

  DeleteBaseObject(list);
  return true;
}
