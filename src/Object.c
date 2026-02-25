/*
 * Tilengine - The 2D retro graphics engine with raster effects
 * Copyright (C) 2015-2019 Marc Palacios Domenech <mailto:megamarc@hotmail.com>
 * All rights reserved
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * */

#include "Object.h"
#include "Engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t numobjects = 0;
static uint32_t numbytes = 0;

static const char *object_types[] = {
    "none",   "palette",  "tilemap",       "tileset",     "spriteset",
    "bitmap", "sequence", "sequence pack", "object list",
};

static const TLN_Error object_errors[] = {
    TLN_ERR_OK,           TLN_ERR_REF_PALETTE,   TLN_ERR_REF_TILEMAP,
    TLN_ERR_REF_TILESET,  TLN_ERR_REF_SPRITESET, TLN_ERR_REF_BITMAP,
    TLN_ERR_REF_SEQUENCE, TLN_ERR_REF_SEQPACK,   TLN_ERR_REF_LIST,
};

/* crea objecto */
void *CreateBaseObject(ObjectType type, int size) {
  object_t *object = (object_t *)malloc(size);
  if (object) {
    char trace_msg[255];
    numobjects++;
    numbytes += size;
    memset(object, 0, size);
    object->type = type;
    object->guid = numobjects;
    object->size = size;
    object->owner = true;
    sprintf(trace_msg, "%s created at %p, %d size", object_types[type], object,
            size);
    tln_trace(TLN_LOG_VERBOSE, trace_msg);
  } else {
    char trace_msg[255];
    TLN_SetLastError(TLN_ERR_OUT_OF_MEMORY);
    sprintf(trace_msg, "failed to create %s!", object_types[type]);
    tln_trace(TLN_LOG_ERRORS, trace_msg);
  }
  return object;
}

/* crea copia de objecto */
void *CloneBaseObject(void *object) {
  object_t const *src = (object_t *)object;
  object_t *dst = (object_t *)CreateBaseObject(src->type, src->size);
  if (dst) {
    memcpy(dst->data, src->data, src->size - sizeof(object_t));
    dst->owner = false;
  }
  return dst;
}

/* elimina objeto */
void DeleteBaseObject(void *object) {
  if (object) {
    char trace_msg[255];
    numobjects--;
    numbytes -= ObjectSize(object);
    sprintf(trace_msg, "%s %p deleted", object_types[ObjectType(object)],
            object);
    tln_trace(TLN_LOG_VERBOSE, trace_msg);
    free(object);
  }
}

/* comprueba tipo de objeto */
bool CheckBaseObject(void *object, ObjectType type) {
  char trace_msg[255];
  if (object != NULL && ObjectType(object) == type)
    return true;

  TLN_SetLastError(object_errors[type]);
  sprintf(trace_msg, "Invalid object address is %p", object);
  tln_trace(TLN_LOG_ERRORS, trace_msg);
  return false;
}

unsigned int GetNumObjects(void) { return numobjects; }

unsigned int GetNumBytes(void) { return numbytes; }

void CopyBaseObject(void *dstobject, void *srcobject) {
  if (srcobject && dstobject)
    memcpy(dstobject, srcobject, ObjectSize(srcobject));
}
