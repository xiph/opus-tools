/* Copyright (C) 2002 Jean-Marc Valin 
   File: wav_io.c
   Routines to handle wav (RIFF) headers

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   
   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
   
   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
   
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <opus/opus_types.h>
#include "wav_io.h"

void write_wav_header(FILE *file, int rate, int channels)
{
   opus_int32 itmp;
   opus_int16 stmp;

   fprintf (file, "RIFF");

   itmp = 0x7fffffff;
   fwrite(&itmp, 4, 1, file);

   fprintf (file, "WAVEfmt ");

   itmp = le_int(16);
   fwrite(&itmp, 4, 1, file);

   stmp = le_short(1);
   fwrite(&stmp, 2, 1, file);

   stmp = le_short(channels);
   fwrite(&stmp, 2, 1, file);

   itmp = le_int(rate);
   fwrite(&itmp, 4, 1, file);

   itmp = le_int(rate*channels*2);
   fwrite(&itmp, 4, 1, file);

   stmp = le_short(2*channels);
   fwrite(&stmp, 2, 1, file);

   stmp = le_short(16);
   fwrite(&stmp, 2, 1, file);

   fprintf (file, "data");

   itmp = le_int(0x7fffffff);
   fwrite(&itmp, 4, 1, file);


}
