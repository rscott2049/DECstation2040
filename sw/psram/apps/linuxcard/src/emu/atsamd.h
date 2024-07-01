/*
	(c) 2021 Dmitry Grinberg   https://dmitry.gr
	Non-commercial use only OR licensing@dmitry.gr
*/

#ifndef _ATSAMD_H_
#define _ATSAMD_H_

#include "samda1e16b.h"

#define NUM_DMA_CHANNELS_USED		1



extern volatile DmacDescriptor mDmaDescrsWriteback[NUM_DMA_CHANNELS_USED];
extern volatile DmacDescriptor mDmaDescrsInitial[NUM_DMA_CHANNELS_USED];
extern volatile DmacDescriptor mDmaDescrsSecond[NUM_DMA_CHANNELS_USED];

//DESCR 0 is for sd, 1+ are for ram


#endif
