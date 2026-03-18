/*
 * ResPacker - General purpose asset packer with AES-128 encryption
 * Copyright (C) 2019 Marc Palacios Domenech <mailto:megamarc@hotmail.com>
 * All rights reserved
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * */

#ifndef RESPACK_H
#define RESPACK_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct ResPack *ResPack;
typedef struct ResAsset *ResAsset;

#ifdef __cplusplus
extern "C" {
#endif

/* opens a resource pack, returns handler. passphrase optional (set for AES-128
 * cypher) */
ResPack ResPack_Open(const char *filename, const char *passphrase);

/* closes an opened resource pack */
void ResPack_Close(ResPack respack);

/* loads contents of asset to memory, returns buffer and actual size */
void *ResPack_LoadAsset(ResPack respack, const char *filename, uint32_t *size);

/* creates a temporal file and opens it, returns asset handler */
ResAsset ResPack_OpenAsset(ResPack respack, const char *filename);

/* returns file handler of an opened asset with ResPack_OpenAsset() */
FILE *ResPack_GetAssetFile(ResAsset asset);

/* returns actual size of an opened asset with ResPack_OpenAsset() */
uint32_t ResPack_GetAssetSize(struct ResAsset const *asset);

/* closes opened asset, deletes temporal file */
void ResPack_CloseAsset(ResAsset asset);

/* builds a resource pack from "filelist" to "filelist.dat", returns number of
 * assets */
int ResPack_Build(const char *filelist, const char *passphrase);

#ifdef __cplusplus
}
#endif

#endif
