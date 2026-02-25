#ifndef _CRT_H
#define _CRT_H

#include <SDL3/SDL.h>
#include <stdbool.h>

typedef enum {
  CRT_SLOT,
  CRT_APERTURE,
  CRT_SHADOW,
} CRTType;

typedef struct _CRTHandler *CRTHandler;

#ifdef __cplusplus
extern "C" {
#endif

CRTHandler CRTCreate(SDL_Renderer *renderer, SDL_Texture *framebuffer,
                     CRTType type, int wnd_width, int wnd_height, bool blur);
void CRTDraw(CRTHandler crt, void *pixels, int pitch, const SDL_FRect *dstrect);
void CRTSetRenderTarget(CRTHandler crt, SDL_Texture *framebuffer);
void CRTIncreaseGlow(CRTHandler crt);
void CRTDecreaseGlow(CRTHandler crt);
void CRTSetBlur(CRTHandler crt, bool blur);
void CRTDelete(CRTHandler crt);

#ifdef __cplusplus
}
#endif

#endif