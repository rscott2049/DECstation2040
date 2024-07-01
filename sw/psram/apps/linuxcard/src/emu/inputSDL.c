/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#include "decPointingDevice.h"
#include "inputSDL.h"
#include "SDL2/SDL.h"
#include "graphics.h"
#include "lk401.h"



static void sdlInputPrvKeyChange(const SDL_KeyboardEvent* evt, bool isKeyDown)
{
	static struct {
		SDL_Keycode sdlKey;
		enum Lk401Key lk401Key;
	}  const keys[] = {
		{SDLK_F1, Lk201_F1},
		{SDLK_F2, Lk201_F2},
		{SDLK_F3, Lk201_F3},
		{SDLK_F4, Lk201_F4},
		{SDLK_F5, Lk201_F5},
		{SDLK_F6, Lk201_F6},
		{SDLK_F7, Lk201_F7},
		{SDLK_F8, Lk201_F8},
		{SDLK_F9, Lk201_F9},
		{SDLK_F10, Lk201_F10},
		{SDLK_F11, Lk201_F11},
		{SDLK_F12, Lk201_F12},
		{SDLK_F13, Lk201_F13},
		{SDLK_F14, Lk201_F14},
		{SDLK_INSERT, Lk201_INSERT},
		{SDLK_DELETE, Lk201_REMOVE}, 
		{SDLK_PAGEUP, Lk201_PREV_SCREEN},
		{SDLK_PAGEDOWN, Lk201_NEXT_SCREEN},
		{SDLK_KP_0, Lk201_KP_0},
		{SDLK_KP_1, Lk201_KP_1},
		{SDLK_KP_2, Lk201_KP_2},
		{SDLK_KP_3, Lk201_KP_3},
		{SDLK_KP_4, Lk201_KP_4},
		{SDLK_KP_5, Lk201_KP_5},
		{SDLK_KP_6, Lk201_KP_6},
		{SDLK_KP_7, Lk201_KP_7},
		{SDLK_KP_8, Lk201_KP_8},
		{SDLK_KP_9, Lk201_KP_9},
		{SDLK_KP_ENTER, Lk201_KP_ENTER},
		{SDLK_KP_PERIOD, Lk201_KP_PERIOD},
		{SDLK_KP_MINUS, Lk201_KP_MINUS}, 
		{SDLK_LEFT, Lk201_LEFT},
		{SDLK_RIGHT, Lk201_RIGHT},
		{SDLK_UP, Lk201_LEFT},
		{SDLK_DOWN, Lk201_LEFT},
		{SDLK_RSHIFT, Lk401_RSHIFT},
		{SDLK_LALT, Lk401_LALT},
		{SDLK_RALT, Lk401_RALT},
		{SDLK_LSHIFT, Lk201_LSHIFT},
		{SDLK_RSHIFT, Lk401_RSHIFT},
		{SDLK_LCTRL, Lk201_CTRL},
		{SDLK_RCTRL, Lk201_CTRL},
		{SDLK_CAPSLOCK, Lk201_LOCK},
		{SDLK_BACKSPACE, Lk201_BACKSPACE},
		{SDLK_RETURN, Lk201_ENTER},
		{SDLK_TAB, Lk201_TAB},
		{SDLK_BACKQUOTE, Lk201_Tilde},
		{SDLK_SPACE, Lk201_SPACE},
		{SDLK_a, Lk201_A},
		{SDLK_b, Lk201_B},
		{SDLK_c, Lk201_C},
		{SDLK_d, Lk201_D},
		{SDLK_e, Lk201_E},
		{SDLK_f, Lk201_F},
		{SDLK_g, Lk201_G},
		{SDLK_h, Lk201_H},
		{SDLK_i, Lk201_I},
		{SDLK_j, Lk201_J},
		{SDLK_k, Lk201_K},
		{SDLK_l, Lk201_L},
		{SDLK_m, Lk201_M},
		{SDLK_n, Lk201_N},
		{SDLK_o, Lk201_O},
		{SDLK_p, Lk201_P},
		{SDLK_q, Lk201_Q},
		{SDLK_r, Lk201_R},
		{SDLK_s, Lk201_S},
		{SDLK_t, Lk201_T},
		{SDLK_u, Lk201_U},
		{SDLK_v, Lk201_V},
		{SDLK_w, Lk201_W},
		{SDLK_x, Lk201_X},
		{SDLK_y, Lk201_Y},
		{SDLK_z, Lk201_Z},
		{SDLK_0, Lk201_0},
		{SDLK_1, Lk201_1},
		{SDLK_2, Lk201_2},
		{SDLK_3, Lk201_3},
		{SDLK_4, Lk201_4},
		{SDLK_5, Lk201_5},
		{SDLK_6, Lk201_6},
		{SDLK_7, Lk201_7},
		{SDLK_8, Lk201_8},
		{SDLK_9, Lk201_9},
		{SDLK_COMMA, Lk201_COMMA},
		{SDLK_PERIOD, Lk201_PERIOD},
		{SDLK_SEMICOLON, Lk201_SEMICOLON},
		{SDLK_SLASH, Lk201_QUESTION},
		{SDLK_EQUALS, Lk201_PLUS},
		{SDLK_MINUS, Lk201_MINUS},
		{SDLK_LEFTBRACKET, Lk201_LBRACE},
		{SDLK_RIGHTBRACKET, Lk201_RBRACE},
		{SDLK_BACKSLASH, Lk201_PIPE},
		{SDLK_QUOTE, Lk201_QOUTE},
	};
	uint_fast8_t i;
	
	//unmapped keys: F16..F20, PF1..PF4, HELP, MENU, FIND, SELECT, KP_COMMA, RCOMPOSE, META, LEFT_RIGHT
	//weird maps: delete->remove
	
	for (i = 0; i < sizeof(keys) / sizeof(*keys); i++) {	//slow, but this is ui code, who cares?
		
		if (keys[i].sdlKey == evt->keysym.sym) {
			
			lk401KeyState(keys[i].lk401Key, isKeyDown);
			return;
		}
	}
}

static void sdlInputPrvMouseButton(Uint8 button, bool down)
{
	enum PointingDeviceButton btn;
	
	switch (button) {
		case SDL_BUTTON_LEFT:
			btn = MouseButtonLeft;
			break;
		
		case SDL_BUTTON_MIDDLE:
			btn = MouseButtonMiddle;
			break;
		
		case SDL_BUTTON_RIGHT:
			btn = MouseButtonRight;
			break;
		
		default:
			return;
	}
	
	decPointingDeviceButton(btn, down);
}
	
static void sdlInputPrvMoveTo(int32_t x, int32_t y)
{
	if (decPointingDeviceIsAbsolute()) {
		
		//abs pointing devices need coords clamped to screen
		if (x < 0)
			x = 0;
		if (x >= SCREEN_WIDTH)
			x = SCREEN_WIDTH - 1;
		if (y < 0)
			y = 0;
		if (y >= SCREEN_HEIGHT)
			y = SCREEN_HEIGHT - 1;
	}
	else {
		
		static uint16_t prevX = 0, prevY = 0;
		int32_t dx, dy;
		
		dx = x - prevX;
		dy = y - prevY;
		prevX = x;
		prevY = y;
		x = dx;
		y = dy;
	}
	
	decPointingDeviceMove(x, y);
}

void sdlInputPoll(void)
{
	SDL_Event event;
	
	while(SDL_PollEvent(&event)) {
		switch(event.type){
			
			case SDL_QUIT:
				exit(0);
				break;
				
			case SDL_KEYDOWN:
				sdlInputPrvKeyChange(&event.key, true);
				break;
			
			case SDL_KEYUP:
				sdlInputPrvKeyChange(&event.key, false);
				break;
			
			case SDL_MOUSEBUTTONDOWN:
				sdlInputPrvMouseButton(event.button.button, true);
				break;
			
			case SDL_MOUSEBUTTONUP:
				sdlInputPrvMouseButton(event.button.button, false);
				break;
			
			case SDL_MOUSEMOTION:
				sdlInputPrvMoveTo(event.motion.x, event.motion.y);
				break;
		}
	}
}
