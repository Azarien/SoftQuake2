/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#include <float.h>

#include "../client/client.h"
#include "../client/snd_loc.h"
#include "winquake.h"

#define iDirectSoundCreate(a,b,c)	pDirectSoundCreate(a,b,c)

HRESULT (WINAPI *pDirectSoundCreate)(GUID FAR *lpGUID, LPDIRECTSOUND FAR *lplpDS, IUnknown FAR *pUnkOuter);

// 64K is > 1 second at 16-bit, 22050 Hz
#define	WAV_BUFFERS				64
#define	WAV_MASK				0x3F
#define	WAV_BUFFER_SIZE			0x0400
#define SECONDARY_BUFFER_SIZE	0x10000

typedef enum {SIS_SUCCESS, SIS_FAILURE, SIS_NOTAVAIL} sndinitstat;

qboolean	dsound_init;
static qboolean	snd_firsttime = true, snd_isdirect;

// starts at 0 for disabled
static int	snd_buffer_count = 0;
static int	sample16;
static int	snd_sent, snd_completed;

/* 
 * Global variables. Must be visible to window-procedure function 
 *  so it can unlock and free the data block after it has been played. 
 */ 


HANDLE		hData;
HPSTR		lpData, lpData2;

DWORD	gSndBufSize;

MMTIME		mmstarttime;

LPDIRECTSOUND pDS;
LPDIRECTSOUNDBUFFER pDSBuf;

qboolean SNDDMA_InitDirect (void);

void FreeSound( void );

static const char *DSoundError( int error )
{
	switch ( error )
	{
	case DSERR_BUFFERLOST:
		return "DSERR_BUFFERLOST";
	case DSERR_INVALIDCALL:
		return "DSERR_INVALIDCALLS";
	case DSERR_INVALIDPARAM:
		return "DSERR_INVALIDPARAM";
	case DSERR_PRIOLEVELNEEDED:
		return "DSERR_PRIOLEVELNEEDED";
	}

	return "unknown";
}

/*
** DS_CreateBuffers
*/
static qboolean DS_CreateBuffers( void )
{
	DSBUFFERDESC	dsbuf;
	DSBCAPS			dsbcaps;
	WAVEFORMATEX	format;
	DWORD			dwWrite;

	memset (&format, 0, sizeof(format));
	format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = dma.channels;
    format.wBitsPerSample = dma.samplebits;
    format.nSamplesPerSec = dma.speed;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.cbSize = 0;
    format.nAvgBytesPerSec = format.nSamplesPerSec*format.nBlockAlign; 

	Com_Printf( "Creating DS buffers\n" );

	Com_DPrintf("...setting PRIORITY coop level: " );
	if ( DS_OK != pDS->lpVtbl->SetCooperativeLevel( pDS, cl_hwnd, DSSCL_PRIORITY ) )
	{
		Com_Printf ("failed\n");
		FreeSound ();
		return false;
	}
	Com_DPrintf("ok\n" );

// create the secondary buffer we'll actually work with
	memset (&dsbuf, 0, sizeof(dsbuf));
	dsbuf.dwSize = sizeof(DSBUFFERDESC);
	dsbuf.dwBufferBytes = SECONDARY_BUFFER_SIZE;
	dsbuf.lpwfxFormat = &format;

	Com_DPrintf( "...creating secondary buffer: " );
	if (DS_OK != pDS->lpVtbl->CreateSoundBuffer(pDS, &dsbuf, &pDSBuf, NULL))
	{
		Com_Printf( "failed\n" );
		FreeSound ();
		return false;
	}
	Com_DPrintf( "ok\n" );

	dma.channels = format.nChannels;
	dma.samplebits = format.wBitsPerSample;
	dma.speed = format.nSamplesPerSec;

	memset(&dsbcaps, 0, sizeof(dsbcaps));
	dsbcaps.dwSize = sizeof(dsbcaps);

	if (DS_OK != pDSBuf->lpVtbl->GetCaps (pDSBuf, &dsbcaps))
	{
		Com_Printf ("*** GetCaps failed ***\n");
		FreeSound ();
		return false;
	}

	Com_Printf ("...using secondary sound buffer\n");

	// Make sure mixer is active
	pDSBuf->lpVtbl->Play(pDSBuf, 0, 0, DSBPLAY_LOOPING);

	if (snd_firsttime)
		Com_Printf("   %d channel(s)\n"
		               "   %d bits/sample\n"
					   "   %d bytes/sec\n",
					   dma.channels, dma.samplebits, dma.speed);
	
	gSndBufSize = dsbcaps.dwBufferBytes;

	/* we don't want anyone to access the buffer directly w/o locking it first. */
	lpData = NULL; 

	pDSBuf->lpVtbl->Stop(pDSBuf);
	pDSBuf->lpVtbl->GetCurrentPosition(pDSBuf, &mmstarttime.u.sample, &dwWrite);
	pDSBuf->lpVtbl->Play(pDSBuf, 0, 0, DSBPLAY_LOOPING);

	dma.samples = gSndBufSize/(dma.samplebits/8);
	dma.samplepos = 0;
	dma.submission_chunk = 1;
	dma.buffer = (unsigned char *) lpData;
	sample16 = (dma.samplebits/8) - 1;

	return true;
}

/*
** DS_DestroyBuffers
*/
static void DS_DestroyBuffers( void )
{
	Com_DPrintf( "Destroying DS buffers\n" );
	if ( pDS )
	{
		Com_DPrintf( "...setting NORMAL coop level\n" );
		pDS->lpVtbl->SetCooperativeLevel( pDS, cl_hwnd, DSSCL_NORMAL );
	}

	if ( pDSBuf )
	{
		Com_DPrintf( "...stopping and releasing sound buffer\n" );
		pDSBuf->lpVtbl->Stop( pDSBuf );
		pDSBuf->lpVtbl->Release( pDSBuf );
	}

	pDSBuf = NULL;

	dma.buffer = NULL;
}

/*
==================
FreeSound
==================
*/
void FreeSound (void)
{
	Com_DPrintf( "Shutting down sound system\n" );

	if ( pDS )
	{
		DS_DestroyBuffers();
		Com_DPrintf( "...releasing DS object\n" );
		pDS->lpVtbl->Release( pDS );
	}

	pDS = NULL;
	pDSBuf = NULL;
	hData = 0;
	lpData = NULL;
	dsound_init = false;
}

/*
==================
SNDDMA_InitDirect

Direct-Sound support
==================
*/
sndinitstat SNDDMA_InitDirect (void)
{
	DSCAPS			dscaps;
	HRESULT			hresult;

	dma.channels = 2;
	dma.samplebits = 16;

	if (s_khz->value == 44)
		dma.speed = 44100;
	if (s_khz->value == 22)
		dma.speed = 22050;
	else
		dma.speed = 11025;

	Com_Printf( "Initializing DirectSound\n");

	Com_DPrintf( "...creating DS object: " );
	while ( ( hresult = DirectSoundCreate( NULL, &pDS, NULL ) ) != DS_OK )
	{
		if (hresult != DSERR_ALLOCATED)
		{
			Com_Printf( "failed\n" );
			return SIS_FAILURE;
		}

		if (MessageBox (NULL,
						"The sound hardware is in use by another app.\n\n"
					    "Select Retry to try to start sound again or Cancel to run Quake with no sound.",
						"Sound not available",
						MB_RETRYCANCEL | MB_SETFOREGROUND | MB_ICONEXCLAMATION) != IDRETRY)
		{
			Com_Printf ("failed, hardware already in use\n" );
			return SIS_NOTAVAIL;
		}
	}
	Com_DPrintf( "ok\n" );

	dscaps.dwSize = sizeof(dscaps);

	if ( DS_OK != pDS->lpVtbl->GetCaps( pDS, &dscaps ) )
	{
		Com_Printf ("*** couldn't get DS caps ***\n");
	}

	if ( dscaps.dwFlags & DSCAPS_EMULDRIVER )
	{
		Com_DPrintf ("...no DSound driver found\n" );
		FreeSound();
		return SIS_FAILURE;
	}

	if ( !DS_CreateBuffers() )
		return SIS_FAILURE;

	dsound_init = true;

	Com_DPrintf("...completed successfully\n" );

	return SIS_SUCCESS;
}


/*
==================
SNDDMA_Init

Try to find a sound device to mix for.
Returns false if nothing is found.
==================
*/
int SNDDMA_Init(void)
{
	sndinitstat	stat;

	memset ((void *)&dma, 0, sizeof (dma));

	dsound_init = 0;

	stat = SIS_FAILURE;	// assume DirectSound won't initialize

	/* Init DirectSound */
	if (snd_firsttime || snd_isdirect)
	{
		stat = SNDDMA_InitDirect ();

		if (stat == SIS_SUCCESS)
		{
			snd_isdirect = true;

			if (snd_firsttime)
				Com_Printf ("dsound init succeeded\n" );
		}
		else
		{
			snd_isdirect = false;
			Com_Printf ("*** dsound init failed ***\n");
		}
	}

	snd_firsttime = false;

	snd_buffer_count = 1;

	if (!dsound_init)
	{
		if (snd_firsttime)
			Com_Printf ("*** No sound device initialized ***\n");

		return 0;
	}

	CDAudio_Init ();

	return 1;
}

/*
==============
SNDDMA_GetDMAPos

return the current sample position (in mono samples read)
inside the recirculating dma buffer, so the mixing code will know
how many sample are required to fill it up.
===============
*/
int SNDDMA_GetDMAPos(void)
{
	MMTIME	mmtime;
	int		s;
	DWORD	dwWrite;

	if (dsound_init) 
	{
		mmtime.wType = TIME_SAMPLES;
		pDSBuf->lpVtbl->GetCurrentPosition(pDSBuf, &mmtime.u.sample, &dwWrite);
		s = mmtime.u.sample - mmstarttime.u.sample;
	}

	s >>= sample16;

	s &= (dma.samples-1);

	return s;
}

/*
==============
SNDDMA_BeginPainting

Makes sure dma.buffer is valid
===============
*/
DWORD	locksize;
void SNDDMA_BeginPainting (void)
{
	int		reps;
	DWORD	dwSize2;
	DWORD	*pbuf, *pbuf2;
	HRESULT	hresult;
	DWORD	dwStatus;

	if (!pDSBuf)
		return;

	// if the buffer was lost or stopped, restore it and/or restart it
	if (pDSBuf->lpVtbl->GetStatus (pDSBuf, &dwStatus) != DS_OK)
		Com_Printf ("Couldn't get sound buffer status\n");
	
	if (dwStatus & DSBSTATUS_BUFFERLOST)
		pDSBuf->lpVtbl->Restore (pDSBuf);
	
	if (!(dwStatus & DSBSTATUS_PLAYING))
		pDSBuf->lpVtbl->Play(pDSBuf, 0, 0, DSBPLAY_LOOPING);

	// lock the dsound buffer

	reps = 0;
	dma.buffer = NULL;

	while ((hresult = pDSBuf->lpVtbl->Lock(pDSBuf, 0, gSndBufSize, &pbuf, &locksize, 
								   &pbuf2, &dwSize2, 0)) != DS_OK)
	{
		if (hresult != DSERR_BUFFERLOST)
		{
			Com_Printf( "S_TransferStereo16: Lock failed with error '%s'\n", DSoundError( hresult ) );
			S_Shutdown ();
			return;
		}
		else
		{
			pDSBuf->lpVtbl->Restore( pDSBuf );
		}

		if (++reps > 2)
			return;
	}
	dma.buffer = (unsigned char *)pbuf;
}

/*
==============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer
Also unlocks the dsound buffer
===============
*/
void SNDDMA_Submit(void)
{
	if (!dma.buffer)
		return;

	// unlock the dsound buffer
	if (pDSBuf)
		pDSBuf->lpVtbl->Unlock(pDSBuf, dma.buffer, locksize, NULL, 0);
}

/*
==============
SNDDMA_Shutdown

Reset the sound device for exiting
===============
*/
void SNDDMA_Shutdown(void)
{
	CDAudio_Shutdown ();
	FreeSound ();
}
