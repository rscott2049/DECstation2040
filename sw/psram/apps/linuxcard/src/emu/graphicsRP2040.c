/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include <stdio.h>
#include "fb_mono.h"
#include "graphics.h"
#include "spiRam.h"
#include "mem.h"
#include "printf.h"
#include "cpu.h"

#define CURSOR_X_OFST		(212)
#define CURSOR_Y_OFST		(34)


struct VdacAccessWindow {
	uint32_t *data;
	uint32_t inProgressWrite, inProgressRead;
	uint8_t mapWA, mapRA, numItemsMask;
};

#define PCC_CMDR_REG_TEST		0x8000
#define PCC_CMDR_REG_HSHI		0x4000
#define PCC_CMDR_REG_VBHI		0x2000
#define PCC_CMDR_REG_LODSA		0x1000
#define PCC_CMDR_REG_FORG2		0x0800
#define PCC_CMDR_REG_ENRG2		0x0400
#define PCC_CMDR_REG_FORG1		0x0200
#define PCC_CMDR_REG_ENRG1		0x0100
#define PCC_CMDR_REG_XHWID		0x0080
#define PCC_CMDR_REG_XHCL1		0x0040
#define PCC_CMDR_REG_XHCLP		0x0020
#define PCC_CMDR_REG_XHAIR		0x0010
#define PCC_CMDR_REG_FOPB		0x0008
#define PCC_CMDR_REG_ENPB		0x0004
#define PCC_CMDR_REG_FOPA		0x0002
#define PCC_CMDR_REG_ENPA		0x0001

static bool mRedrawDue;

static uint32_t mPalette[256], mOverlayColors[16];
static uint8_t mColorPlaneMask;
//static uint8_t mFramebuffer[SCREEN_BYTES];

extern uint32_t mFbBase, mPaletteBase, mCursorBase;

//cursor
static uint16_t mCursorImage[2][16];
static uint16_t mCursorX, mCursorY;
static uint8_t mCursorWritePtr;
static bool mCursorEnabled;

uint32_t grFrame = 0;

void graphicsPeriodic(void)
{
#if 0
  grFrame++;
  if (grFrame > (10 * 70)) {
    grFrame = 0;
    pr("Frame trigger\n");
  }



  //const uint8_t *src = mFramebuffer;
	uint32_t *dst;
	uint32_t r, c;
	
	if (!mRedrawDue)
		return;
	mRedrawDue = false;
	
	//	SDL_LockSurface(mScreen);
	//dst = (uint32_t*)mScreen->pixels;
	
	#if defined(COLOR_FRAMEBUFFER)
	
		for (r = 0; r < SCREEN_HEIGHT; r++, src += SCREEN_STRIDE - SCREEN_WIDTH) {
			for (c = 0; c < SCREEN_WIDTH; c++) {
				
				*dst++ = mPalette[*src++];
			}
		}
		
	#elif defined(MONO_FRAMEBUFFER)
	
		for (r = 0; r < SCREEN_HEIGHT; r++, src += SCREEN_STRIDE - SCREEN_WIDTH / 8) {
			for (c = 0; c < SCREEN_WIDTH / 8; c++) {
				
				uint_fast8_t v = *src++;
				uint32_t p;
				
				for (p = 0; p < 8; p++, v >>= 1)
					*dst++ = mPalette[(v & 1) ? 0xff : 0];
			}
		}
	
	#else
	
		#error "no framebuffer defined and yet graphics code being compiled in..."
	
	#endif
	
	if (mCursorEnabled) {
#if 0		
		int32_t cr, cc, sr = (int32_t)(uint32_t)mCursorY - CURSOR_Y_OFST, sc = (int32_t)(uint32_t)mCursorX - CURSOR_X_OFST;
		const uint16_t *planeA = mCursorImage[0], *planeB = mCursorImage[1];
		
		//dst = (uint32_t*)mScreen->pixels;
		dst += sr * SCREEN_WIDTH + sc;	//might be offscreen in either direction
		
		for (cr = 0; cr < 16; cr++, sr++, sc -= 16, dst += SCREEN_WIDTH - 16) {
			
			uint_fast16_t mask = 1, dataA = *planeA++, dataB = *planeB++;
			
			if (sr < 0 || sr >= SCREEN_HEIGHT)
				continue;
			
			for (cc = 0; cc < 16; cc++, sc++, mask <<= 1, dst++) {
				
				uint32_t color = 0;
				
				if (sc < 0 || sc >= SCREEN_WIDTH)
					continue;
				
				if (dataA & mask)
					color += 8;
				
				if (dataB & mask)
					color += 4;
				
				if (color)
					*dst = mOverlayColors[color];
			}
		}
#endif
	}
	
#if 0
	SDL_UnlockSurface(mScreen);
	SDL_BlitSurface(mScreen, NULL, SDL_GetWindowSurface(mWindow), NULL);
	SDL_UpdateWindowSurface(mWindow);
#endif
#endif
}

static void gfxPrvRequestRedraw(void)
{
	mRedrawDue = true;
}

//we assume sane cursor display config
static bool gfxPrvCursor(uint32_t paOfst, uint_fast8_t size, bool write, void* buf)
{
	bool redrawCursor = false;
	uint_fast16_t v;
	
	//write only regs
	if (paOfst & 3)
		return false;
	
	if (!write) {
		switch (size) {
			case 1:
				*(uint8_t*)buf = 0;
				return true;
			case 2:
				*(uint16_t*)buf = 0;
				return true;
			case 4:
				*(uint32_t*)buf = 0;
				return true;
			default:
				return false;
		}
	}
	//word writes only
	if (size != 2)
		return false;
	
	v = *(uint16_t*)buf;
	
	switch (paOfst / 4) {
		case 0x00 / 4:	//CMDR
			if (v & PCC_CMDR_REG_LODSA) {
				if (mCursorEnabled)
					redrawCursor = true;
				mCursorEnabled = false;
				mCursorWritePtr = 0;
			}
			else {
				if (!mCursorEnabled)
					redrawCursor = true;
				mCursorEnabled = true;
			}
			break;
		
		case 0x04 / 4:	//XPOS
			redrawCursor = (mCursorX != v);
			mCursorX = v;
			break;
		
		case 0x08 / 4:	//YPOS
			redrawCursor = (mCursorY != v);
			mCursorY = v;
			break;
		
		case 0x0c / 4:	//XMIN1
		case 0x10 / 4:	//XMAX1
		case 0x14 / 4:	//YMIN1
		case 0x18 / 4:	//YMAX1
		case 0x2c / 4:	//XMIN2
		case 0x30 / 4:	//XMAX2
		case 0x34 / 4:	//YMIN2
		case 0x38 / 4:	//YMAX2
			//we do not support crosshair cursors - they are stupid
			break;
		
		case 0x3c / 4:	//memory load
			//first A plane then B plane
			//mCursorImage[mCursorWritePtr / 16][mCursorWritePtr % 16] = v;
		  if (mCursorWritePtr < 16) {
		    cursor_planeA[mCursorWritePtr & 0x0f] = v;
		  } else {
		    cursor_planeB[mCursorWritePtr & 0x0f] = v;
		  }

			if (++mCursorWritePtr == 32)
				mCursorWritePtr = 0;
			break;
		
		default:
			return false;
	}
	
	if (redrawCursor) {
	  //gfxPrvRequestRedraw();
	  fb_mono_set_cursor_pos(mCursorX - CURSOR_X_OFST,
				 mCursorY - CURSOR_Y_OFST);
	}

	return true;
}

static bool gfxPrvColorPlaneMask(uint32_t paOfst, uint_fast8_t size, bool write, void* buf)
{
	if (paOfst)
		return false;
	
	if (write) switch (size) {
		case 1:
			mColorPlaneMask = *(uint8_t*)buf;
			return true;
		
		case 2:
			mColorPlaneMask = *(uint16_t*)buf;
			return true;
		
		case 4:
			mColorPlaneMask = *(uint32_t*)buf;
			return true;
		
		default:
			return false;
	}
	else switch (size) {
		case 1:
			*(uint8_t*)buf = mColorPlaneMask;
			return true;
		
		case 2:
			*(uint16_t*)buf = mColorPlaneMask;
			return true;
		
		case 4:
			*(uint32_t*)buf = mColorPlaneMask;
			return true;
		
		default:
			return false;
	}
}

static bool gfxPrvVdac(uint32_t paOfst, uint_fast8_t size, bool write, void* buf)
{

	static struct VdacAccessWindow windows[2] = {
		[0] = {.data = mPalette, .inProgressWrite = 0x01000000, .numItemsMask = 0xff, },
		[1] = {.data = mOverlayColors, .inProgressWrite = 0x01000000, .numItemsMask = 0x0f, },
	};
	struct VdacAccessWindow *win;
	bool changed = false;
	uint_fast16_t v;
	
	switch (size) {
		case 1:
			v = *(uint8_t*)buf;
			break;
		
		case 2:
			v = *(uint16_t*)buf;
			break;
		
		default:
			return false;
	}
	
	if (paOfst & 3)
		return false;
	
	if (paOfst >= 0x20)		//not documented but accessed by prom
		return write;
	
	win = &windows[paOfst >> 4];
	switch ((paOfst / 4) & 3) {
		case 0:		//WA
			if (write)
				win->mapWA = v & win->numItemsMask;
			else
				v = win->mapWA;
			break;
		
		case 1:		//access
			if (write) {
				win->inProgressWrite = (win->inProgressWrite >> 8) + (((uint32_t)((v & 0xff))) << 24);
				if (win->inProgressWrite & 0xff) {
					
					uint32_t prevVal = win->data[win->mapWA];
					win->data[win->mapWA] = win->inProgressWrite >> 8;
					changed = (win->inProgressWrite >> 8) != prevVal;
					win->inProgressWrite = 0x01000000;
					win->mapWA = (win->mapWA + 1) & win->numItemsMask;
					

				}
			}
			else {
				if (!win->inProgressRead)
					win->inProgressRead = win->data[win->mapRA] + 0x01000000;
				v = win->inProgressRead & 0xff;
				win->inProgressRead >>= 8;
				if (win->inProgressRead == 0x01) {
					win->inProgressRead = 0;
					win->mapRA = (win->mapRA + 1) & win->numItemsMask;
				}
			}
			break;
		
		case 2:
			if (write) {
				if (v != 0xff)
					return false;
			}
			else
				v = 0xff;
			break;
		
		case 3:
			if (write)
				win->mapRA = v & win->numItemsMask;
			else
				v = win->mapRA;
			break;
		
		default:
			__builtin_unreachable();
			break;
	}
	
	if (!write) switch (size) {
		case 1:
			*(uint8_t*)buf = v;
			break;
		
		case 2:
			*(uint16_t*)buf = v;
			break;
		
		default:
			__builtin_unreachable();
			break;
	}
	
	if (changed) {
	  //gfxPrvRequestRedraw();
	  // Update overlay colors
	  fb_mono_set_overlay_color(1, mOverlayColors[4]);
	  fb_mono_set_overlay_color(2, mOverlayColors[8]);
	  fb_mono_set_overlay_color(3, mOverlayColors[12]);
	}	

	return true;
}

void graphicsSetStart(uint32_t mFbBase, uint32_t mPaletteBase, uint32_t mCursorBase) {

  fb_mono_set_fb_start(mFbBase);
}

static bool gfxPrvFramebuffer(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
  //pr("wr/adr: %d %08x\n", write, pa);

	if (pa >= SCREEN_BYTES)
		return false;

	if (write)
		spiRamWrite(mFbBase + pa, buf, size);
	else
		spiRamRead(mFbBase + pa, buf, size);

	return true;
#if 0
	if (write) {
		
		uint32_t prev, now;
		
		switch (size) {
			case 1:
				prev = mFramebuffer[pa];
				mFramebuffer[pa] = now = *(const uint8_t*)buf;
				break;
			
			case 2:
				prev = *(uint16_t*)(mFramebuffer + pa);
				*(uint16_t*)(mFramebuffer + pa) = now = *(const uint16_t*)buf;
				break;
			
			case 4:
				prev = *(uint32_t*)(mFramebuffer + pa);
				*(uint32_t*)(mFramebuffer + pa) = now = *(const uint32_t*)buf;
				break;
			
			default:
				return false;
		}
		if (now != prev)
			gfxPrvRequestRedraw();
		
		return true;
	}
	else switch (size) {
		case 1:
			*(uint8_t*)buf = mFramebuffer[pa];
			return true;
		
		case 2:
			*(uint16_t*)buf = *(uint16_t*)(mFramebuffer + pa);
			return true;
		
		case 4:
			*(uint32_t*)buf = *(uint32_t*)(mFramebuffer + pa);
			return true;
		
		default:
			return false;
	}
#endif
}

uint32_t grAccess = 3;

static bool graphicsMemAccess(uint32_t pa, uint_fast8_t size, bool write, void* buf)
{
#if 0
  pr("gfx mem access wr/adr: %d %08x\n", write, pa);

  if ((grAccess--) == 0) {
    pr("sleeping ...\n");
    //prTLB();
    while(1){}
  } else {
    pr("grAccess: %d\n", grAccess);
  }
#endif
	if (pa >= 0x12000000)
		return gfxPrvVdac(pa - 0x12000000, size, write, buf);
	if (pa >= 0x11000000)
		return gfxPrvCursor(pa - 0x11000000, size, write, buf);
	if (pa >= 0x10000000)
		return gfxPrvColorPlaneMask(pa - 0x10000000, size, write, buf);
	if (pa < 0x0fd00000)
		return gfxPrvFramebuffer(pa - 0x0fc00000, size, write, buf);
	return false;
}

bool graphicsInit(void)
{

#ifdef NO_FRAMEBUFFER
	return memRegionAdd(0x0fc00000, 0x03400000, graphicsMemAccess);
#else
	//uint32_t setup_mode = PREFERRED_VID_MODE;
	uint32_t setup_mode = 2;
	//uint32_t setup_mode = 6;
	// For minimal memory usage
	//uint32_t setup_mode = 0;
	//uint32_t setup_mode = -1;

	uint32_t ret_mode = fb_mono_init(setup_mode);
	
	if ((setup_mode != -1) && (ret_mode == setup_mode)){
	  pr("Using %d x %d video format\n", _inst.hactive, _inst.vactive);
	  fb_mono_irq_en(_inst.vbp, 1);
	  fb_mono_cb_addr = graphicsPeriodic;
	  pr("cb: %08x\n", (uint32_t)fb_mono_cb_addr);
	  (*fb_mono_cb_addr)();
	} else {
	  pr("Video mode setup error or video not enabled\n");
	}

	return memRegionAdd(0x0fc00000, 0x03400000, graphicsMemAccess);
#endif

}

