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
/*
** RW_DDRAW.C
**
** This handles DirecTDraw management under Windows.
*/
#ifndef _WIN32
#  error You should not be compiling this file on this platform
#endif

#include <float.h>

#include "..\ref_soft\r_local.h"
#define INITGUID
#include "rw_win.h"

static const char *DDrawError( int code );

/*
** DDRAW_Init
**
** Builds our DDRAW stuff
*/
qboolean DDRAW_Init( unsigned char **ppbuffer, int *ppitch )
{
	HRESULT ddrval;
	DDSURFACEDESC2 ddsd;

	HRESULT (WINAPI *QDirectDrawCreateEx)( GUID FAR *lpGUID, void FAR * lplpDDRAW, REFGUID iid, IUnknown FAR * pUnkOuter );

ri.Con_Printf( PRINT_ALL, "Initializing DirectDraw\n");

	/*
	** load DLL and fetch pointer to entry point
	*/
	if ( !sww_state.hinstDDRAW )
	{
		ri.Con_Printf( PRINT_ALL, "...loading DDRAW.DLL: ");
		if ( ( sww_state.hinstDDRAW = LoadLibrary( "ddraw.dll" ) ) == NULL )
		{
			ri.Con_Printf( PRINT_ALL, "failed\n" );
			goto fail;
		}
		ri.Con_Printf( PRINT_ALL, "ok\n" );
	}

	if ( ( QDirectDrawCreateEx = ( HRESULT (WINAPI *)( GUID FAR *, void FAR * lplpDDRAW, REFGUID iid, IUnknown FAR * ) ) GetProcAddress( sww_state.hinstDDRAW, "DirectDrawCreateEx" ) ) == NULL )
	{
		ri.Con_Printf( PRINT_ALL, "*** DirectDrawCreateEx == NULL ***\n" );
		goto fail;
	}

	/*
	** create the direct draw object
	*/
	ri.Con_Printf( PRINT_ALL, "...creating DirectDraw object: ");
	if ( ( ddrval = QDirectDrawCreateEx( NULL, &sww_state.lpDirectDraw, &IID_IDirectDraw7, NULL ) ) != DD_OK )
	{
		ri.Con_Printf( PRINT_ALL, "failed - %s\n", DDrawError( ddrval ) );
		goto fail;
	}
	ri.Con_Printf( PRINT_ALL, "ok\n" );

	/*
	** see if linear modes exist first
	*/

	ri.Con_Printf( PRINT_ALL, "...setting exclusive mode: ");
	if ( ( ddrval = sww_state.lpDirectDraw->lpVtbl->SetCooperativeLevel( sww_state.lpDirectDraw, 
																		 sww_state.hWnd,
																		 DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN ) ) != DD_OK )
	{
		ri.Con_Printf( PRINT_ALL, "failed - %s\n",DDrawError (ddrval) );
		goto fail;
	}
	ri.Con_Printf( PRINT_ALL, "ok\n" );

	/*
	** try changing the display mode normally
	*/
	ri.Con_Printf( PRINT_ALL, "...finding display mode\n" );
	ri.Con_Printf( PRINT_ALL, "...setting linear mode: " );
	if ( ( ddrval = sww_state.lpDirectDraw->lpVtbl->SetDisplayMode( sww_state.lpDirectDraw, vid.width, vid.height, 32, 0, 0 ) ) == DD_OK )
	{
		ri.Con_Printf( PRINT_ALL, "ok\n" );
	}
	else
	{
		ri.Con_Printf( PRINT_ALL, "failed\n" );
		goto fail;
	}

	/*
	** create our front buffer
	*/
	memset( &ddsd, 0, sizeof( ddsd ) );
	ddsd.dwSize = sizeof( ddsd );
	ddsd.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
	ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
	ddsd.dwBackBufferCount = 1;

	ri.Con_Printf( PRINT_ALL, "...creating front buffer: ");
	if ( ( ddrval = sww_state.lpDirectDraw->lpVtbl->CreateSurface( sww_state.lpDirectDraw, &ddsd, &sww_state.lpddsFrontBuffer, NULL ) ) != DD_OK )
	{
		ri.Con_Printf( PRINT_ALL, "failed - %s\n", DDrawError( ddrval ) );
		goto fail;
	}
	ri.Con_Printf( PRINT_ALL, "ok\n" );

	/*
	** create our back buffer
	*/
	ddsd.ddsCaps.dwCaps = DDSCAPS_BACKBUFFER;

	ri.Con_Printf( PRINT_ALL, "...creating back buffer: " );
	if ( ( ddrval = sww_state.lpddsFrontBuffer->lpVtbl->GetAttachedSurface( sww_state.lpddsFrontBuffer, &ddsd.ddsCaps, &sww_state.lpddsBackBuffer ) ) != DD_OK )
	{
		ri.Con_Printf( PRINT_ALL, "failed - %s\n", DDrawError( ddrval ) );
		goto fail;
	}
	ri.Con_Printf( PRINT_ALL, "ok\n" );

	/*
	** create our rendering buffer
	*/
	ri.Con_Printf( PRINT_ALL, "...creating offscreen buffer: " );
	sww_state.lpOffScreenBuffer = malloc(vid.width * vid.height);
	memset(sww_state.lpOffScreenBuffer, 255, vid.width * vid.height);
	ri.Con_Printf( PRINT_ALL, "ok\n" );

	*ppbuffer = sww_state.lpOffScreenBuffer;
	*ppitch   = vid.width;

	sww_state.palettized = true;

	sww_state.fakePalette = malloc(1024);

	return true;
fail:
	ri.Con_Printf( PRINT_ALL, "*** DDraw init failure ***\n" );

	DDRAW_Shutdown();
	return false;
}

void DDRAW_SetPalette( const unsigned char *_pal )
{
	// convert RGB0 to BGR0
	int i;
	for (i=0; i<=255; i++)
	{
		sww_state.fakePalette[i*4 + 0] = _pal[i*4 + 2]; // B
		sww_state.fakePalette[i*4 + 1] = _pal[i*4 + 1]; // G
		sww_state.fakePalette[i*4 + 2] = _pal[i*4 + 0]; // R
		sww_state.fakePalette[i*4 + 3] = 255;           // x
	}
}

/*
** DDRAW_Shutdown
*/
void DDRAW_Shutdown( void )
{
	if ( sww_state.fakePalette )
	{
		free(sww_state.fakePalette);
		sww_state.fakePalette = NULL;
	}
	if ( sww_state.lpOffScreenBuffer )
	{
		ri.Con_Printf( PRINT_ALL, "...releasing offscreen buffer\n");
		free(sww_state.lpOffScreenBuffer);
		sww_state.lpOffScreenBuffer = NULL;
	}

	if ( sww_state.lpddsBackBuffer )
	{
		ri.Con_Printf( PRINT_ALL, "...releasing back buffer\n");
		sww_state.lpddsBackBuffer->lpVtbl->Release( sww_state.lpddsBackBuffer );
		sww_state.lpddsBackBuffer = NULL;
	}

	if ( sww_state.lpddsFrontBuffer )
	{
		ri.Con_Printf( PRINT_ALL, "...releasing front buffer\n");
		sww_state.lpddsFrontBuffer->lpVtbl->Release( sww_state.lpddsFrontBuffer );
		sww_state.lpddsFrontBuffer = NULL;
	}

	if ( sww_state.lpDirectDraw )
	{
		ri.Con_Printf( PRINT_ALL, "...restoring display mode\n");
		sww_state.lpDirectDraw->lpVtbl->RestoreDisplayMode( sww_state.lpDirectDraw );
		ri.Con_Printf( PRINT_ALL, "...restoring normal coop mode\n");
	    sww_state.lpDirectDraw->lpVtbl->SetCooperativeLevel( sww_state.lpDirectDraw, sww_state.hWnd, DDSCL_NORMAL );
		ri.Con_Printf( PRINT_ALL, "...releasing lpDirectDraw\n");
		sww_state.lpDirectDraw->lpVtbl->Release( sww_state.lpDirectDraw );
		sww_state.lpDirectDraw = NULL;
	}

	if ( sww_state.hinstDDRAW )
	{
		ri.Con_Printf( PRINT_ALL, "...freeing library\n");
		FreeLibrary( sww_state.hinstDDRAW );
		sww_state.hinstDDRAW = NULL;
	}
}

static const char *DDrawError (int code)
{
    switch(code) {
        case DD_OK:
            return "DD_OK";
        case DDERR_ALREADYINITIALIZED:
            return "DDERR_ALREADYINITIALIZED";
        case DDERR_BLTFASTCANTCLIP:
            return "DDERR_BLTFASTCANTCLIP";
        case DDERR_CANNOTATTACHSURFACE:
            return "DDER_CANNOTATTACHSURFACE";
        case DDERR_CANNOTDETACHSURFACE:
            return "DDERR_CANNOTDETACHSURFACE";
        case DDERR_CANTCREATEDC:
            return "DDERR_CANTCREATEDC";
        case DDERR_CANTDUPLICATE:
            return "DDER_CANTDUPLICATE";
        case DDERR_CLIPPERISUSINGHWND:
            return "DDER_CLIPPERUSINGHWND";
        case DDERR_COLORKEYNOTSET:
            return "DDERR_COLORKEYNOTSET";
        case DDERR_CURRENTLYNOTAVAIL:
            return "DDERR_CURRENTLYNOTAVAIL";
        case DDERR_DIRECTDRAWALREADYCREATED:
            return "DDERR_DIRECTDRAWALREADYCREATED";
        case DDERR_EXCEPTION:
            return "DDERR_EXCEPTION";
        case DDERR_EXCLUSIVEMODEALREADYSET:
            return "DDERR_EXCLUSIVEMODEALREADYSET";
        case DDERR_GENERIC:
            return "DDERR_GENERIC";
        case DDERR_HEIGHTALIGN:
            return "DDERR_HEIGHTALIGN";
        case DDERR_HWNDALREADYSET:
            return "DDERR_HWNDALREADYSET";
        case DDERR_HWNDSUBCLASSED:
            return "DDERR_HWNDSUBCLASSED";
        case DDERR_IMPLICITLYCREATED:
            return "DDERR_IMPLICITLYCREATED";
        case DDERR_INCOMPATIBLEPRIMARY:
            return "DDERR_INCOMPATIBLEPRIMARY";
        case DDERR_INVALIDCAPS:
            return "DDERR_INVALIDCAPS";
        case DDERR_INVALIDCLIPLIST:
            return "DDERR_INVALIDCLIPLIST";
        case DDERR_INVALIDDIRECTDRAWGUID:
            return "DDERR_INVALIDDIRECTDRAWGUID";
        case DDERR_INVALIDMODE:
            return "DDERR_INVALIDMODE";
        case DDERR_INVALIDOBJECT:
            return "DDERR_INVALIDOBJECT";
        case DDERR_INVALIDPARAMS:
            return "DDERR_INVALIDPARAMS";
        case DDERR_INVALIDPIXELFORMAT:
            return "DDERR_INVALIDPIXELFORMAT";
        case DDERR_INVALIDPOSITION:
            return "DDERR_INVALIDPOSITION";
        case DDERR_INVALIDRECT:
            return "DDERR_INVALIDRECT";
        case DDERR_LOCKEDSURFACES:
            return "DDERR_LOCKEDSURFACES";
        case DDERR_NO3D:
            return "DDERR_NO3D";
        case DDERR_NOALPHAHW:
            return "DDERR_NOALPHAHW";
        case DDERR_NOBLTHW:
            return "DDERR_NOBLTHW";
        case DDERR_NOCLIPLIST:
            return "DDERR_NOCLIPLIST";
        case DDERR_NOCLIPPERATTACHED:
            return "DDERR_NOCLIPPERATTACHED";
        case DDERR_NOCOLORCONVHW:
            return "DDERR_NOCOLORCONVHW";
        case DDERR_NOCOLORKEY:
            return "DDERR_NOCOLORKEY";
        case DDERR_NOCOLORKEYHW:
            return "DDERR_NOCOLORKEYHW";
        case DDERR_NOCOOPERATIVELEVELSET:
            return "DDERR_NOCOOPERATIVELEVELSET";
        case DDERR_NODC:
            return "DDERR_NODC";
        case DDERR_NODDROPSHW:
            return "DDERR_NODDROPSHW";
        case DDERR_NODIRECTDRAWHW:
            return "DDERR_NODIRECTDRAWHW";
        case DDERR_NOEMULATION:
            return "DDERR_NOEMULATION";
        case DDERR_NOEXCLUSIVEMODE:
            return "DDERR_NOEXCLUSIVEMODE";
        case DDERR_NOFLIPHW:
            return "DDERR_NOFLIPHW";
        case DDERR_NOGDI:
            return "DDERR_NOGDI";
        case DDERR_NOHWND:
            return "DDERR_NOHWND";
        case DDERR_NOMIRRORHW:
            return "DDERR_NOMIRRORHW";
        case DDERR_NOOVERLAYDEST:
            return "DDERR_NOOVERLAYDEST";
        case DDERR_NOOVERLAYHW:
            return "DDERR_NOOVERLAYHW";
        case DDERR_NOPALETTEATTACHED:
            return "DDERR_NOPALETTEATTACHED";
        case DDERR_NOPALETTEHW:
            return "DDERR_NOPALETTEHW";
        case DDERR_NORASTEROPHW:
            return "Operation could not be carried out because there is no appropriate raster op hardware present or available.\0";
        case DDERR_NOROTATIONHW:
            return "Operation could not be carried out because there is no rotation hardware present or available.\0";
        case DDERR_NOSTRETCHHW:
            return "Operation could not be carried out because there is no hardware support for stretching.\0";
        case DDERR_NOT4BITCOLOR:
            return "DirectDrawSurface is not in 4 bit color palette and the requested operation requires 4 bit color palette.\0";
        case DDERR_NOT4BITCOLORINDEX:
            return "DirectDrawSurface is not in 4 bit color index palette and the requested operation requires 4 bit color index palette.\0";
        case DDERR_NOT8BITCOLOR:
            return "DDERR_NOT8BITCOLOR";
        case DDERR_NOTAOVERLAYSURFACE:
            return "Returned when an overlay member is called for a non-overlay surface.\0";
        case DDERR_NOTEXTUREHW:
            return "Operation could not be carried out because there is no texture mapping hardware present or available.\0";
        case DDERR_NOTFLIPPABLE:
            return "DDERR_NOTFLIPPABLE";
        case DDERR_NOTFOUND:
            return "DDERR_NOTFOUND";
        case DDERR_NOTLOCKED:
            return "DDERR_NOTLOCKED";
        case DDERR_NOTPALETTIZED:
            return "DDERR_NOTPALETTIZED";
        case DDERR_NOVSYNCHW:
            return "DDERR_NOVSYNCHW";
        case DDERR_NOZBUFFERHW:
            return "Operation could not be carried out because there is no hardware support for zbuffer blitting.\0";
        case DDERR_NOZOVERLAYHW:
            return "Overlay surfaces could not be z layered based on their BltOrder because the hardware does not support z layering of overlays.\0";
        case DDERR_OUTOFCAPS:
            return "The hardware needed for the requested operation has already been allocated.\0";
        case DDERR_OUTOFMEMORY:
            return "DDERR_OUTOFMEMORY";
        case DDERR_OUTOFVIDEOMEMORY:
            return "DDERR_OUTOFVIDEOMEMORY";
        case DDERR_OVERLAYCANTCLIP:
            return "The hardware does not support clipped overlays.\0";
        case DDERR_OVERLAYCOLORKEYONLYONEACTIVE:
            return "Can only have ony color key active at one time for overlays.\0";
        case DDERR_OVERLAYNOTVISIBLE:
            return "Returned when GetOverlayPosition is called on a hidden overlay.\0";
        case DDERR_PALETTEBUSY:
            return "DDERR_PALETTEBUSY";
        case DDERR_PRIMARYSURFACEALREADYEXISTS:
            return "DDERR_PRIMARYSURFACEALREADYEXISTS";
        case DDERR_REGIONTOOSMALL:
            return "Region passed to Clipper::GetClipList is too small.\0";
        case DDERR_SURFACEALREADYATTACHED:
            return "DDERR_SURFACEALREADYATTACHED";
        case DDERR_SURFACEALREADYDEPENDENT:
            return "DDERR_SURFACEALREADYDEPENDENT";
        case DDERR_SURFACEBUSY:
            return "DDERR_SURFACEBUSY";
        case DDERR_SURFACEISOBSCURED:
            return "Access to surface refused because the surface is obscured.\0";
        case DDERR_SURFACELOST:
            return "DDERR_SURFACELOST";
        case DDERR_SURFACENOTATTACHED:
            return "DDERR_SURFACENOTATTACHED";
        case DDERR_TOOBIGHEIGHT:
            return "Height requested by DirectDraw is too large.\0";
        case DDERR_TOOBIGSIZE:
            return "Size requested by DirectDraw is too large, but the individual height and width are OK.\0";
        case DDERR_TOOBIGWIDTH:
            return "Width requested by DirectDraw is too large.\0";
        case DDERR_UNSUPPORTED:
            return "DDERR_UNSUPPORTED";
        case DDERR_UNSUPPORTEDFORMAT:
            return "FOURCC format requested is unsupported by DirectDraw.\0";
        case DDERR_UNSUPPORTEDMASK:
            return "Bitmask in the pixel format requested is unsupported by DirectDraw.\0";
        case DDERR_VERTICALBLANKINPROGRESS:
            return "Vertical blank is in progress.\0";
        case DDERR_WASSTILLDRAWING:
            return "DDERR_WASSTILLDRAWING";
        case DDERR_WRONGMODE:
            return "This surface can not be restored because it was created in a different mode.\0";
        case DDERR_XALIGN:
            return "Rectangle provided was not horizontally aligned on required boundary.\0";
        default:
            return "UNKNOWN\0";
	}
}

