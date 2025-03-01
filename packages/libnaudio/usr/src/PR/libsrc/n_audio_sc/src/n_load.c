/*====================================================================
 *
 * Copyright 1993, Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Silicon Graphics,
 * Inc.; the contents of this file may not be disclosed to third
 * parties, copied or duplicated in any form, in whole or in part,
 * without the prior written permission of Silicon Graphics, Inc.
 *
 * RESTRICTED RIGHTS LEGEND:
 * Use, duplication or disclosure by the Government is subject to
 * restrictions as set forth in subdivision (c)(1)(ii) of the Rights
 * in Technical Data and Computer Software clause at DFARS
 * 252.227-7013, and/or in similar or successor clauses in the FAR,
 * DOD or NASA FAR Supplement. Unpublished - rights reserved under the
 * Copyright Laws of the United States.
 *====================================================================*/

#include "n_synthInternals.h"
#include <os.h>
#include <R4300.h>

#ifndef MIN
#   define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifdef AUD_PROFILE
extern u32 cnt_index, adpcm_num, adpcm_cnt, adpcm_max, adpcm_min, lastCnt[];
#endif

#define ADPCMFBYTES      9
#define LFSAMPLES        4

static
  Acmd *_decodeChunk(Acmd *ptr, N_PVoice *f, s32 tsam, s32 nbytes, s16 outp, s16 inp, u32 flags);

Acmd *n_alAdpcmPull(N_PVoice *filter, s16 *outp, s32 outCount, Acmd *p) 
{
  Acmd        *ptr = p;
  s16         inp;
  s32         tsam;
  s32         nframes;
  s32         nbytes;
  s32         overFlow;
  s32         startZero;
  s32         nOver;
  s32         nSam;
  s32         op;
  s32         nLeft;
  s32         bEnd;
  s32         decoded = 0;
  s32         looped = 0;
  
  N_PVoice *f = filter;
  
#ifdef AUD_PROFILE
  lastCnt[++cnt_index] = osGetCount();
#endif
  
  if (outCount == 0)
    return ptr;

#ifndef N_MICRO  
  inp = AL_DECODER_IN;
#else
  inp = N_AL_DECODER_IN;
#endif

  aLoadADPCM(ptr++, f->dc_bookSize,
	     K0_TO_PHYS(f->dc_table->waveInfo.adpcmWave.book->book));
  
  looped = (outCount + f->dc_sample > f->dc_loop.end) && (f->dc_loop.count != 0);
  if (looped)
    nSam = f->dc_loop.end - f->dc_sample;
  else
    nSam = outCount;
  
  if (f->dc_lastsam)
    nLeft = ADPCMFSIZE - f->dc_lastsam;
  else
    nLeft = 0;
  tsam = nSam - nLeft;
  if (tsam<0) tsam = 0;
  
  nframes = (tsam+ADPCMFSIZE-1)>>LFSAMPLES;
  nbytes =  nframes*ADPCMFBYTES;
  
  if (looped){
    
    ptr = _decodeChunk(ptr, f, tsam, nbytes, *outp, inp, f->dc_first);
    
    /*
     * Fix up output pointer, which will be used as the input pointer
     * by the following module.
     */
    if (f->dc_lastsam)
      *outp += (f->dc_lastsam<<1);
    else
      *outp += (ADPCMFSIZE<<1);
    
    /*
     * Now fix up state info to reflect the loop start point
     */
    f->dc_lastsam = f->dc_loop.start &0xf;
    f->dc_memin = (s32) f->dc_table->base + ADPCMFBYTES *
      ((s32) (f->dc_loop.start>>LFSAMPLES) + 1);
    f->dc_sample = f->dc_loop.start;
    
    bEnd = *outp;
    while (outCount > nSam){
      
      outCount -= nSam;
      
      /*
       * Put next one after the end of the last lot - on the
       * frame boundary (32 byte) after the end.
       */
#if 0
      op = (bEnd + ((nframes+1)<<(LFSAMPLES+1))) & ~0x1f;
#else
      op = (bEnd + ((nframes+1)<<(LFSAMPLES+1)) + 16) & ~0x1f;
#endif
      
      /*
       * The actual end of data
       */
      bEnd += (nSam<<1);
      
      /*
       * -1 is loop forever - the loop count is not exact now
       * for small loops!
       */
      if ((f->dc_loop.count != -1) && (f->dc_loop.count != 0))
	f->dc_loop.count--;
      
      /*
       * What's left to compute.
       */
      nSam = MIN(outCount, f->dc_loop.end - f->dc_loop.start);
      tsam = nSam - ADPCMFSIZE + f->dc_lastsam;  
      if (tsam<0) tsam = 0;
      nframes = (tsam+ADPCMFSIZE-1)>>LFSAMPLES;
      nbytes =  nframes*ADPCMFBYTES;
      ptr = _decodeChunk(ptr, f, tsam, nbytes, op, inp, f->dc_first | A_LOOP);
      /*
       * Merge the two sections in DMEM.
       */
      aDMEMMove(ptr++, op+(f->dc_lastsam<<1), bEnd, nSam<<1);
    }
    
    f->dc_lastsam = (outCount + f->dc_lastsam) & 0xf;
    f->dc_sample += outCount;
    f->dc_memin += ADPCMFBYTES*nframes;    
#ifdef AUD_PROFILE
    PROFILE_AUD(adpcm_num, adpcm_cnt, adpcm_max, adpcm_min);
#endif
    return ptr;
  }
  
  /*
   * The unlooped case, which is executed most of the time
   */
  
  nSam = nframes<<LFSAMPLES;
  
  /*
   * overFlow is the number of bytes past the end
   * of the bitstream I try to generate
   */
  overFlow = f->dc_memin + nbytes - ((s32) f->dc_table->base + f->dc_table->len);
  if (overFlow < 0)
    overFlow = 0;
  nOver = (overFlow/ADPCMFBYTES)<<LFSAMPLES;
  if (nOver > nSam + nLeft)
    nOver = nSam + nLeft;
  
  nbytes -= overFlow;
  
  if ((nOver - (nOver & 0xf))< outCount){
    decoded = 1;
    ptr = _decodeChunk(ptr, f, nSam - nOver, nbytes, *outp, inp, f->dc_first);
    
    if (f->dc_lastsam)
      *outp += (f->dc_lastsam<<1);
    else
      *outp += (ADPCMFSIZE<<1);
    
    f->dc_lastsam = (outCount + f->dc_lastsam) & 0xf;
    f->dc_sample += outCount;
    f->dc_memin += ADPCMFBYTES*nframes;    
  } else {        
    f->dc_lastsam = 0;
    f->dc_memin += ADPCMFBYTES*nframes;    
  }
  
  /*
   * Put zeros in if necessary
   */
  if (nOver){
    f->dc_lastsam = 0;
    if (decoded)
      startZero = (nLeft + nSam - nOver)<<1;
    else
      startZero = 0;
    aClearBuffer(ptr++, startZero + *outp, nOver<<1);
  }
#ifdef AUD_PROFILE
  PROFILE_AUD(adpcm_num, adpcm_cnt, adpcm_max, adpcm_min);
#endif
  
  return ptr;
}

s32
  n_alLoadParam(N_PVoice *filter, s32 paramID, void *param)
{
  N_PVoice *a = filter;
#if 0
  ALFilter *f = (ALFilter *) filter;
#endif
  
  switch (paramID) {
  case (AL_FILTER_SET_WAVETABLE):
    a->dc_table = (ALWaveTable *) param;
    a->dc_memin = (s32) a->dc_table->base;
    a->dc_sample = 0;
    switch (a->dc_table->type){
    case (AL_ADPCM_WAVE):
      
      a->dc_table->len = ADPCMFBYTES *
	((s32) (a->dc_table->len/ADPCMFBYTES));
      
      a->dc_bookSize = 2*a->dc_table->waveInfo.adpcmWave.book->order*
	a->dc_table->waveInfo.adpcmWave.book->npredictors*ADPCMVSIZE;
      if (a->dc_table->waveInfo.adpcmWave.loop) {
	a->dc_loop.start = a->dc_table->waveInfo.adpcmWave.loop->start;
	a->dc_loop.end = a->dc_table->waveInfo.adpcmWave.loop->end;
	a->dc_loop.count = a->dc_table->waveInfo.adpcmWave.loop->count;
	alCopy(a->dc_table->waveInfo.adpcmWave.loop->state,
	       a->dc_lstate, sizeof(ADPCM_STATE));
      } else {
	a->dc_loop.start = a->dc_loop.end = a->dc_loop.count = 0;
      }
      break;
      
    case (AL_RAW16_WAVE):
      if (a->dc_table->waveInfo.rawWave.loop) {
	a->dc_loop.start = a->dc_table->waveInfo.rawWave.loop->start;
	a->dc_loop.end = a->dc_table->waveInfo.rawWave.loop->end;
	a->dc_loop.count = a->dc_table->waveInfo.rawWave.loop->count;
      } else {
	a->dc_loop.start = a->dc_loop.end = a->dc_loop.count = 0;
      }
      break;
      
    default:
      break;
      
    }
    break;
    
  case (AL_FILTER_RESET):
    a->dc_lastsam = 0;
    a->dc_first   = 1;
    a->dc_sample = 0;
    
    /* sct 2/14/96 - Check table since it is initialized to null and */
    /* Get loop info according to table type. */
    if (a->dc_table)
      {
	a->dc_memin  = (s32) a->dc_table->base;
	if (a->dc_table->type == AL_ADPCM_WAVE)
	  {
	    if (a->dc_table->waveInfo.adpcmWave.loop)
	      a->dc_loop.count = a->dc_table->waveInfo.adpcmWave.loop->count;
	  }
	else if (a->dc_table->type == AL_RAW16_WAVE)
	  {
	    if (a->dc_table->waveInfo.rawWave.loop)
	      a->dc_loop.count = a->dc_table->waveInfo.rawWave.loop->count;
	  }
      }
    
    break;
    
  default:
    break;
  }
}

Acmd *_decodeChunk(Acmd *ptr, N_PVoice *f, s32 tsam,
		   s32 nbytes, s16 outp, s16 inp, u32 flags)
{
  s32 dramAlign, dramLoc;
  
  if (nbytes > 0){
    dramLoc = (f->dc_dma)(f->dc_memin, nbytes, f->dc_dmaState);
    /*
     * Make sure enough is loaded into DMEM to take care
     * of 8 byte alignment
     */
    dramAlign = dramLoc & 0x7;
    nbytes += dramAlign;
#ifndef N_MICRO
    aSetBuffer(ptr++, 0, inp, 0, nbytes + 8 - (nbytes & 0x7));
    aLoadBuffer(ptr++, dramLoc - dramAlign);
#else
#include "inc/n_load_add01.inc.c"
#endif
  } else
    dramAlign = 0;
  
  if (flags & A_LOOP){
    aSetLoop(ptr++, K0_TO_PHYS(f->dc_lstate));
  }

#ifndef N_MICRO
  aSetBuffer(ptr++, 0, inp + dramAlign, outp, tsam<<1);
  aADPCMdec(ptr++, flags, K0_TO_PHYS(f->dc_state));
#else
#include "inc/n_load_add02.inc.c"
#endif
  f->dc_first = 0;
  return ptr;
}
