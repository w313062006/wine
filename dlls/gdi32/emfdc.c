/*
 * Enhanced MetaFile recording functions
 *
 * Copyright 1999 Huw D M Davies
 * Copyright 2016 Alexandre Julliard
 * Copyright 2021 Jacek Caban for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <assert.h>

#include "enhmfdrv/enhmetafiledrv.h"
#include "winnls.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(enhmetafile);


static const RECTL empty_bounds = { 0, 0, -1, -1 };

BOOL emfdc_record( struct emf *emf, EMR *emr )
{
    DWORD len, size;
    ENHMETAHEADER *emh;

    TRACE( "record %d, size %d\n", emr->iType, emr->nSize );

    assert( !(emr->nSize & 3) );

    emf->emh->nBytes += emr->nSize;
    emf->emh->nRecords++;

    size = HeapSize( GetProcessHeap(), 0, emf->emh );
    len = emf->emh->nBytes;
    if (len > size)
    {
        size += (size / 2) + emr->nSize;
        emh = HeapReAlloc( GetProcessHeap(), 0, emf->emh, size );
        if (!emh) return FALSE;
        emf->emh = emh;
    }
    memcpy( (char *)emf->emh + emf->emh->nBytes - emr->nSize, emr, emr->nSize );
    return TRUE;
}

void emfdc_update_bounds( struct emf *emf, RECTL *rect )
{
    RECTL *bounds = &emf->dc_attr->emf_bounds;
    RECTL vport_rect = *rect;

    LPtoDP( emf->dc_attr->hdc, (POINT *)&vport_rect, 2 );

    /* The coordinate systems may be mirrored
       (LPtoDP handles points, not rectangles) */
    if (vport_rect.left > vport_rect.right)
    {
        LONG temp = vport_rect.right;
        vport_rect.right = vport_rect.left;
        vport_rect.left = temp;
    }
    if (vport_rect.top > vport_rect.bottom)
    {
        LONG temp = vport_rect.bottom;
        vport_rect.bottom = vport_rect.top;
        vport_rect.top = temp;
    }

    if (bounds->left > bounds->right)
    {
        /* first bounding rectangle */
        *bounds = vport_rect;
    }
    else
    {
        bounds->left   = min(bounds->left,   vport_rect.left);
        bounds->top    = min(bounds->top,    vport_rect.top);
        bounds->right  = max(bounds->right,  vport_rect.right);
        bounds->bottom = max(bounds->bottom, vport_rect.bottom);
    }
}

static UINT emfdc_add_handle( struct emf *emf, HGDIOBJ obj )
{
    UINT index;

    for (index = 0; index < emf->handles_size; index++)
        if (emf->handles[index] == 0) break;

    if (index == emf->handles_size)
    {
        emf->handles_size += HANDLE_LIST_INC;
        emf->handles = HeapReAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY,
                                    emf->handles,
                                    emf->handles_size * sizeof(emf->handles[0]) );
    }
    emf->handles[index] = get_full_gdi_handle( obj );

    emf->cur_handles++;
    if (emf->cur_handles > emf->emh->nHandles)
        emf->emh->nHandles++;

    return index + 1; /* index 0 is reserved for the hmf, so we increment everything by 1 */
}

static UINT emfdc_find_object( struct emf *emf, HGDIOBJ obj )
{
    UINT index;

    for (index = 0; index < emf->handles_size; index++)
        if (emf->handles[index] == obj) return index + 1;

    return 0;
}

static void emfdc_delete_object( HDC hdc, HGDIOBJ obj )
{
    DC_ATTR *dc_attr = get_dc_attr( hdc );
    struct emf *emf = dc_attr->emf;
    EMRDELETEOBJECT emr;
    UINT index;

    if(!(index = emfdc_find_object( emf, obj ))) return;

    emr.emr.iType = EMR_DELETEOBJECT;
    emr.emr.nSize = sizeof(emr);
    emr.ihObject = index;

    emfdc_record( emf, &emr.emr );

    emf->handles[index - 1] = 0;
    emf->cur_handles--;
}

static DWORD emfdc_create_brush( struct emf *emf, HBRUSH brush )
{
    DWORD index = 0;
    LOGBRUSH logbrush;

    if (!GetObjectA( brush, sizeof(logbrush), &logbrush )) return 0;

    switch (logbrush.lbStyle) {
    case BS_SOLID:
    case BS_HATCHED:
    case BS_NULL:
        {
            EMRCREATEBRUSHINDIRECT emr;
            emr.emr.iType = EMR_CREATEBRUSHINDIRECT;
            emr.emr.nSize = sizeof(emr);
            emr.ihBrush = index = emfdc_add_handle( emf, brush );
            emr.lb.lbStyle = logbrush.lbStyle;
            emr.lb.lbColor = logbrush.lbColor;
            emr.lb.lbHatch = logbrush.lbHatch;

            if(!emfdc_record( emf, &emr.emr ))
                index = 0;
        }
      break;
    case BS_PATTERN:
    case BS_DIBPATTERN:
        {
            EMRCREATEDIBPATTERNBRUSHPT *emr;
            char buffer[FIELD_OFFSET( BITMAPINFO, bmiColors[256] )];
            BITMAPINFO *info = (BITMAPINFO *)buffer;
            DWORD info_size;
            void *bits;
            UINT usage;

            if (!get_brush_bitmap_info( brush, info, &bits, &usage )) break;
            info_size = get_dib_info_size( info, usage );

            emr = HeapAlloc( GetProcessHeap(), 0,
                             sizeof(EMRCREATEDIBPATTERNBRUSHPT)+info_size+info->bmiHeader.biSizeImage );
            if(!emr) break;

            if (logbrush.lbStyle == BS_PATTERN && info->bmiHeader.biBitCount == 1)
            {
                /* Presumably to reduce the size of the written EMF, MS supports an
                 * undocumented iUsage value of 2, indicating a mono bitmap without the
                 * 8 byte 2 entry black/white palette. Stupidly, they could have saved
                 * over 20 bytes more by also ignoring the BITMAPINFO fields that are
                 * irrelevant/constant for monochrome bitmaps.
                 * FIXME: It may be that the DIB functions themselves accept this value.
                 */
                emr->emr.iType = EMR_CREATEMONOBRUSH;
                usage = DIB_PAL_MONO;
                /* FIXME: There is an extra DWORD written by native before the BMI.
                 *        Not sure what it's meant to contain.
                 */
                emr->offBmi = sizeof( EMRCREATEDIBPATTERNBRUSHPT ) + sizeof(DWORD);
                emr->cbBmi = sizeof( BITMAPINFOHEADER );
            }
            else
            {
                emr->emr.iType = EMR_CREATEDIBPATTERNBRUSHPT;
                emr->offBmi = sizeof( EMRCREATEDIBPATTERNBRUSHPT );
                emr->cbBmi = info_size;
            }
            emr->ihBrush = index = emfdc_add_handle( emf, brush );
            emr->iUsage = usage;
            emr->offBits = emr->offBmi + emr->cbBmi;
            emr->cbBits = info->bmiHeader.biSizeImage;
            emr->emr.nSize = emr->offBits + emr->cbBits;

            memcpy( (BYTE *)emr + emr->offBmi, info, emr->cbBmi );
            memcpy( (BYTE *)emr + emr->offBits, bits, emr->cbBits );

            if (!emfdc_record( emf, &emr->emr )) index = 0;
            HeapFree( GetProcessHeap(), 0, emr );
        }
        break;

    default:
        FIXME("Unknown style %x\n", logbrush.lbStyle);
        break;
    }

    return index;
}

static BOOL emfdc_select_brush( DC_ATTR *dc_attr, HBRUSH brush )
{
    struct emf *emf = dc_attr->emf;
    EMRSELECTOBJECT emr;
    DWORD index = 0;
    int i;

    /* If the object is a stock brush object, do not need to create it.
     * See definitions in  wingdi.h for range of stock brushes.
     * We do however have to handle setting the higher order bit to
     * designate that this is a stock object.
     */
    for (i = WHITE_BRUSH; i <= DC_BRUSH; i++)
    {
        if (brush == GetStockObject(i))
        {
            index = i | 0x80000000;
            break;
        }
    }

    if (!index && !(index = emfdc_find_object( emf, brush )))
    {
        if (!(index = emfdc_create_brush( emf, brush ))) return 0;
        GDI_hdc_using_object( brush, dc_attr->hdc, emfdc_delete_object );
    }

    emr.emr.iType = EMR_SELECTOBJECT;
    emr.emr.nSize = sizeof(emr);
    emr.ihObject = index;
    return emfdc_record( emf, &emr.emr );
}

static BOOL emfdc_create_font( struct emf *emf, HFONT font )
{
    DWORD index = 0;
    EMREXTCREATEFONTINDIRECTW emr;
    int i;

    if (!GetObjectW( font, sizeof(emr.elfw.elfLogFont), &emr.elfw.elfLogFont )) return FALSE;

    emr.emr.iType = EMR_EXTCREATEFONTINDIRECTW;
    emr.emr.nSize = (sizeof(emr) + 3) / 4 * 4;
    emr.ihFont = index = emfdc_add_handle( emf, font );
    emr.elfw.elfFullName[0] = '\0';
    emr.elfw.elfStyle[0]    = '\0';
    emr.elfw.elfVersion     = 0;
    emr.elfw.elfStyleSize   = 0;
    emr.elfw.elfMatch       = 0;
    emr.elfw.elfReserved    = 0;
    for (i = 0; i < ELF_VENDOR_SIZE; i++)
        emr.elfw.elfVendorId[i] = 0;
    emr.elfw.elfCulture                 = PAN_CULTURE_LATIN;
    emr.elfw.elfPanose.bFamilyType      = PAN_NO_FIT;
    emr.elfw.elfPanose.bSerifStyle      = PAN_NO_FIT;
    emr.elfw.elfPanose.bWeight          = PAN_NO_FIT;
    emr.elfw.elfPanose.bProportion      = PAN_NO_FIT;
    emr.elfw.elfPanose.bContrast        = PAN_NO_FIT;
    emr.elfw.elfPanose.bStrokeVariation = PAN_NO_FIT;
    emr.elfw.elfPanose.bArmStyle        = PAN_NO_FIT;
    emr.elfw.elfPanose.bLetterform      = PAN_NO_FIT;
    emr.elfw.elfPanose.bMidline         = PAN_NO_FIT;
    emr.elfw.elfPanose.bXHeight         = PAN_NO_FIT;

    return emfdc_record( emf, &emr.emr ) ? index : 0;
}

static BOOL emfdc_select_font( DC_ATTR *dc_attr, HFONT font )
{
    struct emf *emf = dc_attr->emf;
    EMRSELECTOBJECT emr;
    DWORD index;
    int i;

    /* If the object is a stock font object, do not need to create it.
     * See definitions in  wingdi.h for range of stock fonts.
     * We do however have to handle setting the higher order bit to
     * designate that this is a stock object.
     */

    for (i = OEM_FIXED_FONT; i <= DEFAULT_GUI_FONT; i++)
    {
        if (i != DEFAULT_PALETTE && font == GetStockObject(i))
        {
            index = i | 0x80000000;
            goto found;
        }
    }

    if (!(index = emfdc_find_object( emf, font )))
    {
        if (!(index = emfdc_create_font( emf, font ))) return FALSE;
        GDI_hdc_using_object( font, dc_attr->hdc, emfdc_delete_object );
    }

 found:
    emr.emr.iType = EMR_SELECTOBJECT;
    emr.emr.nSize = sizeof(emr);
    emr.ihObject = index;
    return emfdc_record( emf, &emr.emr );
}

static DWORD emfdc_create_pen( struct emf *emf, HPEN hPen )
{
    EMRCREATEPEN emr;
    DWORD index = 0;

    if (!GetObjectW( hPen, sizeof(emr.lopn), &emr.lopn ))
    {
        /* must be an extended pen */
        EXTLOGPEN *elp;
        INT size = GetObjectW( hPen, 0, NULL );

        if (!size) return 0;

        elp = HeapAlloc( GetProcessHeap(), 0, size );

        GetObjectW( hPen, size, elp );
        /* FIXME: add support for user style pens */
        emr.lopn.lopnStyle = elp->elpPenStyle;
        emr.lopn.lopnWidth.x = elp->elpWidth;
        emr.lopn.lopnWidth.y = 0;
        emr.lopn.lopnColor = elp->elpColor;

        HeapFree( GetProcessHeap(), 0, elp );
    }

    emr.emr.iType = EMR_CREATEPEN;
    emr.emr.nSize = sizeof(emr);
    emr.ihPen = index = emfdc_add_handle( emf, hPen );
    return emfdc_record( emf, &emr.emr ) ? index : 0;
}

static BOOL emfdc_select_pen( DC_ATTR *dc_attr, HPEN pen )
{
    struct emf *emf = dc_attr->emf;
    EMRSELECTOBJECT emr;
    DWORD index = 0;
    int i;

    /* If the object is a stock pen object, do not need to create it.
     * See definitions in  wingdi.h for range of stock pens.
     * We do however have to handle setting the higher order bit to
     * designate that this is a stock object.
     */

    for (i = WHITE_PEN; i <= DC_PEN; i++)
    {
        if (pen == GetStockObject(i))
        {
            index = i | 0x80000000;
            break;
        }
    }
    if (!index && !(index = emfdc_find_object( emf, pen )))
    {
        if (!(index = emfdc_create_pen( emf, pen ))) return FALSE;
        GDI_hdc_using_object( pen, dc_attr->hdc, emfdc_delete_object );
    }

    emr.emr.iType = EMR_SELECTOBJECT;
    emr.emr.nSize = sizeof(emr);
    emr.ihObject = index;
    return emfdc_record( emf, &emr.emr );
}

static DWORD emfdc_create_palette( struct emf *emf, HPALETTE hPal )
{
    WORD i;
    struct {
        EMRCREATEPALETTE hdr;
        PALETTEENTRY entry[255];
    } pal;

    memset( &pal, 0, sizeof(pal) );

    if (!GetObjectW( hPal, sizeof(pal.hdr.lgpl) + sizeof(pal.entry), &pal.hdr.lgpl ))
        return 0;

    for (i = 0; i < pal.hdr.lgpl.palNumEntries; i++)
        pal.hdr.lgpl.palPalEntry[i].peFlags = 0;

    pal.hdr.emr.iType = EMR_CREATEPALETTE;
    pal.hdr.emr.nSize = sizeof(pal.hdr) + pal.hdr.lgpl.palNumEntries * sizeof(PALETTEENTRY);
    pal.hdr.ihPal = emfdc_add_handle( emf, hPal );

    if (!emfdc_record( emf, &pal.hdr.emr ))
        pal.hdr.ihPal = 0;
    return pal.hdr.ihPal;
}

BOOL EMFDC_SelectPalette( DC_ATTR *dc_attr, HPALETTE palette )
{
    struct emf *emf = dc_attr->emf;
    EMRSELECTPALETTE emr;
    DWORD index = 0;

    if (palette == GetStockObject( DEFAULT_PALETTE ))
    {
        index = DEFAULT_PALETTE | 0x80000000;
    }
    else if (!(index = emfdc_find_object( emf, palette )))
    {
        if (!(index = emfdc_create_palette( emf, palette ))) return 0;
        GDI_hdc_using_object( palette, dc_attr->hdc, emfdc_delete_object );
    }

    emr.emr.iType = EMR_SELECTPALETTE;
    emr.emr.nSize = sizeof(emr);
    emr.ihPal = index;
    return emfdc_record( emf, &emr.emr );
}

BOOL EMFDC_SelectObject( DC_ATTR *dc_attr, HGDIOBJ obj )
{
    switch (gdi_handle_type( obj ))
    {
    case NTGDI_OBJ_BRUSH:
        return emfdc_select_brush( dc_attr, obj );
    case NTGDI_OBJ_FONT:
        return emfdc_select_font( dc_attr, obj );
    case NTGDI_OBJ_PEN:
    case NTGDI_OBJ_EXTPEN:
        return emfdc_select_pen( dc_attr, obj );
    default:
        return TRUE;
    }
}

/* determine if we can use 16-bit points to store all the input points */
static BOOL can_use_short_points( const POINT *pts, UINT count )
{
    UINT i;

    for (i = 0; i < count; i++)
        if (((pts[i].x + 0x8000) & ~0xffff) || ((pts[i].y + 0x8000) & ~0xffff))
            return FALSE;
    return TRUE;
}

/* store points in either long or short format; return a pointer to the end of the stored data */
static void *store_points( POINTL *dest, const POINT *pts, UINT count, BOOL short_points )
{
    if (short_points)
    {
        UINT i;
        POINTS *dest_short = (POINTS *)dest;

        for (i = 0; i < count; i++)
        {
            dest_short[i].x = pts[i].x;
            dest_short[i].y = pts[i].y;
        }
        return dest_short + count;
    }
    else
    {
        memcpy( dest, pts, count * sizeof(*dest) );
        return dest + count;
    }
}

/* compute the bounds of an array of points, optionally including the current position */
static void get_points_bounds( RECTL *bounds, const POINT *pts, UINT count, DC_ATTR *dc_attr )
{
    UINT i;

    if (dc_attr)
    {
        bounds->left = bounds->right = dc_attr->cur_pos.x;
        bounds->top = bounds->bottom = dc_attr->cur_pos.y;
    }
    else if (count)
    {
        bounds->left = bounds->right = pts[0].x;
        bounds->top = bounds->bottom = pts[0].y;
    }
    else *bounds = empty_bounds;

    for (i = 0; i < count; i++)
    {
        bounds->left   = min( bounds->left, pts[i].x );
        bounds->right  = max( bounds->right, pts[i].x );
        bounds->top    = min( bounds->top, pts[i].y );
        bounds->bottom = max( bounds->bottom, pts[i].y );
    }
}

/* helper for path stroke and fill functions */
static BOOL emfdrv_stroke_and_fill_path( struct emf *emf, INT type )
{
    EMRSTROKEANDFILLPATH emr;
    HRGN region;

    emr.emr.iType = type;
    emr.emr.nSize = sizeof(emr);
    emr.rclBounds = empty_bounds;

    if ((region = NtGdiPathToRegion( emf->dc_attr->hdc )))
    {
        NtGdiGetRgnBox( region, (RECT *)&emr.rclBounds );
        DeleteObject( region );
    }

    if (!emfdc_record( emf, &emr.emr )) return FALSE;
    if (!region) return FALSE;
    emfdc_update_bounds( emf, &emr.rclBounds );
    return TRUE;
}

BOOL EMFDC_MoveTo( DC_ATTR *dc_attr, INT x, INT y )
{
    struct emf *emf = dc_attr->emf;
    EMRMOVETOEX emr;

    emr.emr.iType = EMR_MOVETOEX;
    emr.emr.nSize = sizeof(emr);
    emr.ptl.x = x;
    emr.ptl.y = y;
    return emfdc_record( emf, &emr.emr );
}

BOOL EMFDC_LineTo( DC_ATTR *dc_attr, INT x, INT y )
{
    EMRLINETO emr;

    emr.emr.iType = EMR_LINETO;
    emr.emr.nSize = sizeof(emr);
    emr.ptl.x = x;
    emr.ptl.y = y;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_ArcChordPie( DC_ATTR *dc_attr, INT left, INT top, INT right, INT bottom,
                        INT xstart, INT ystart, INT xend, INT yend, DWORD type )
{
    struct emf *emf = dc_attr->emf;
    EMRARC emr;
    INT temp;

    if (left == right || top == bottom) return FALSE;

    if (left > right) { temp = left; left = right; right = temp; }
    if (top > bottom) { temp = top; top = bottom; bottom = temp; }

    if (dc_attr->graphics_mode == GM_COMPATIBLE)
    {
        right--;
        bottom--;
    }

    emr.emr.iType     = type;
    emr.emr.nSize     = sizeof(emr);
    emr.rclBox.left   = left;
    emr.rclBox.top    = top;
    emr.rclBox.right  = right;
    emr.rclBox.bottom = bottom;
    emr.ptlStart.x    = xstart;
    emr.ptlStart.y    = ystart;
    emr.ptlEnd.x      = xend;
    emr.ptlEnd.y      = yend;
    return emfdc_record( emf, &emr.emr );
}

BOOL EMFDC_AngleArc( DC_ATTR *dc_attr, INT x, INT y, DWORD radius, FLOAT start, FLOAT sweep )
{
    EMRANGLEARC emr;

    emr.emr.iType   = EMR_ANGLEARC;
    emr.emr.nSize   = sizeof( emr );
    emr.ptlCenter.x = x;
    emr.ptlCenter.y = y;
    emr.nRadius     = radius;
    emr.eStartAngle = start;
    emr.eSweepAngle = sweep;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_Ellipse( DC_ATTR *dc_attr, INT left, INT top, INT right, INT bottom )
{
    struct emf *emf = dc_attr->emf;
    EMRELLIPSE emr;

    if (left == right || top == bottom) return FALSE;

    emr.emr.iType     = EMR_ELLIPSE;
    emr.emr.nSize     = sizeof(emr);
    emr.rclBox.left   = min( left, right );
    emr.rclBox.top    = min( top, bottom );
    emr.rclBox.right  = max( left, right );
    emr.rclBox.bottom = max( top, bottom );
    if (dc_attr->graphics_mode == GM_COMPATIBLE)
    {
        emr.rclBox.right--;
        emr.rclBox.bottom--;
    }
    return emfdc_record( emf, &emr.emr );
}

BOOL EMFDC_Rectangle( DC_ATTR *dc_attr, INT left, INT top, INT right, INT bottom )
{
    struct emf *emf = dc_attr->emf;
    EMRRECTANGLE emr;

    if(left == right || top == bottom) return FALSE;

    emr.emr.iType     = EMR_RECTANGLE;
    emr.emr.nSize     = sizeof(emr);
    emr.rclBox.left   = min( left, right );
    emr.rclBox.top    = min( top, bottom );
    emr.rclBox.right  = max( left, right );
    emr.rclBox.bottom = max( top, bottom );
    if (dc_attr->graphics_mode == GM_COMPATIBLE)
    {
        emr.rclBox.right--;
        emr.rclBox.bottom--;
    }
    return emfdc_record( emf, &emr.emr );
}

BOOL EMFDC_RoundRect( DC_ATTR *dc_attr, INT left, INT top, INT right,
                      INT bottom, INT ell_width, INT ell_height )
{
    struct emf *emf = dc_attr->emf;
    EMRROUNDRECT emr;

    if (left == right || top == bottom) return FALSE;

    emr.emr.iType     = EMR_ROUNDRECT;
    emr.emr.nSize     = sizeof(emr);
    emr.rclBox.left   = min( left, right );
    emr.rclBox.top    = min( top, bottom );
    emr.rclBox.right  = max( left, right );
    emr.rclBox.bottom = max( top, bottom );
    emr.szlCorner.cx  = ell_width;
    emr.szlCorner.cy  = ell_height;
    if (dc_attr->graphics_mode == GM_COMPATIBLE)
    {
        emr.rclBox.right--;
        emr.rclBox.bottom--;
    }
    return emfdc_record( emf, &emr.emr );
}

BOOL EMFDC_SetPixel( DC_ATTR *dc_attr, INT x, INT y, COLORREF color )
{
    EMRSETPIXELV emr;

    emr.emr.iType  = EMR_SETPIXELV;
    emr.emr.nSize  = sizeof(emr);
    emr.ptlPixel.x = x;
    emr.ptlPixel.y = y;
    emr.crColor = color;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

static BOOL emfdc_polylinegon( DC_ATTR *dc_attr, const POINT *points, INT count, DWORD type )
{
    struct emf *emf = dc_attr->emf;
    EMRPOLYLINE *emr;
    DWORD size;
    BOOL ret, use_small_emr = can_use_short_points( points, count );

    size = use_small_emr ? offsetof( EMRPOLYLINE16, apts[count] ) : offsetof( EMRPOLYLINE, aptl[count] );

    emr = HeapAlloc( GetProcessHeap(), 0, size );
    emr->emr.iType = use_small_emr ? type + EMR_POLYLINE16 - EMR_POLYLINE : type;
    emr->emr.nSize = size;
    emr->cptl = count;

    store_points( emr->aptl, points, count, use_small_emr );

    if (!emf->path)
        get_points_bounds( &emr->rclBounds, points, count,
                           (type == EMR_POLYBEZIERTO || type == EMR_POLYLINETO) ? dc_attr : 0 );
    else
        emr->rclBounds = empty_bounds;

    ret = emfdc_record( emf, &emr->emr );
    if (ret && !emf->path) emfdc_update_bounds( emf, &emr->rclBounds );
    HeapFree( GetProcessHeap(), 0, emr );
    return ret;
}

BOOL EMFDC_Polyline( DC_ATTR *dc_attr, const POINT *points, INT count )
{
    return emfdc_polylinegon( dc_attr, points, count, EMR_POLYLINE );
}

BOOL EMFDC_PolylineTo( DC_ATTR *dc_attr, const POINT *points, INT count )
{
    return emfdc_polylinegon( dc_attr, points, count, EMR_POLYLINETO );
}

BOOL EMFDC_Polygon( DC_ATTR *dc_attr, const POINT *pt, INT count )
{
    if(count < 2) return FALSE;
    return emfdc_polylinegon( dc_attr, pt, count, EMR_POLYGON );
}

BOOL EMFDC_PolyBezier( DC_ATTR *dc_attr, const POINT *pts, DWORD count )
{
    return emfdc_polylinegon( dc_attr, pts, count, EMR_POLYBEZIER );
}

BOOL EMFDC_PolyBezierTo( DC_ATTR *dc_attr, const POINT *pts, DWORD count )
{
    return emfdc_polylinegon( dc_attr, pts, count, EMR_POLYBEZIERTO );
}

static BOOL emfdc_poly_polylinegon( struct emf *emf, const POINT *pt, const INT *counts,
                                   UINT polys, DWORD type)
{
    EMRPOLYPOLYLINE *emr;
    DWORD cptl = 0, poly, size;
    BOOL ret, use_small_emr, bounds_valid = TRUE;

    for(poly = 0; poly < polys; poly++) {
        cptl += counts[poly];
        if(counts[poly] < 2) bounds_valid = FALSE;
    }
    if(!cptl) bounds_valid = FALSE;
    use_small_emr = can_use_short_points( pt, cptl );

    size = FIELD_OFFSET(EMRPOLYPOLYLINE, aPolyCounts[polys]);
    if(use_small_emr)
        size += cptl * sizeof(POINTS);
    else
        size += cptl * sizeof(POINTL);

    emr = HeapAlloc( GetProcessHeap(), 0, size );

    emr->emr.iType = type;
    if(use_small_emr) emr->emr.iType += EMR_POLYPOLYLINE16 - EMR_POLYPOLYLINE;

    emr->emr.nSize = size;
    if(bounds_valid && !emf->path)
        get_points_bounds( &emr->rclBounds, pt, cptl, 0 );
    else
        emr->rclBounds = empty_bounds;
    emr->nPolys = polys;
    emr->cptl = cptl;

    if(polys)
    {
        memcpy( emr->aPolyCounts, counts, polys * sizeof(DWORD) );
        store_points( (POINTL *)(emr->aPolyCounts + polys), pt, cptl, use_small_emr );
    }

    ret = emfdc_record( emf, &emr->emr );
    if(ret && !bounds_valid)
    {
        ret = FALSE;
        SetLastError( ERROR_INVALID_PARAMETER );
    }
    if(ret && !emf->path)
        emfdc_update_bounds( emf, &emr->rclBounds );
    HeapFree( GetProcessHeap(), 0, emr );
    return ret;
}

BOOL EMFDC_PolyPolyline( DC_ATTR *dc_attr, const POINT *pt, const DWORD *counts, DWORD polys)
{
    return emfdc_poly_polylinegon( dc_attr->emf, pt, (const INT *)counts, polys, EMR_POLYPOLYLINE );
}

BOOL EMFDC_PolyPolygon( DC_ATTR *dc_attr, const POINT *pt, const INT *counts, UINT polys )
{
    return emfdc_poly_polylinegon( dc_attr->emf, pt, counts, polys, EMR_POLYPOLYGON );
}

BOOL EMFDC_PolyDraw( DC_ATTR *dc_attr, const POINT *pts, const BYTE *types, DWORD count )
{
    struct emf *emf = dc_attr->emf;
    EMRPOLYDRAW *emr;
    BOOL ret;
    BYTE *types_dest;
    BOOL use_small_emr = can_use_short_points( pts, count );
    DWORD size;

    size = use_small_emr ? offsetof( EMRPOLYDRAW16, apts[count] )
        : offsetof( EMRPOLYDRAW, aptl[count] );
    size += (count + 3) & ~3;

    if (!(emr = HeapAlloc( GetProcessHeap(), 0, size ))) return FALSE;

    emr->emr.iType = use_small_emr ? EMR_POLYDRAW16 : EMR_POLYDRAW;
    emr->emr.nSize = size;
    emr->cptl = count;

    types_dest = store_points( emr->aptl, pts, count, use_small_emr );
    memcpy( types_dest, types, count );
    if (count & 3) memset( types_dest + count, 0, 4 - (count & 3) );

    if (!emf->path)
        get_points_bounds( &emr->rclBounds, pts, count, 0 );
    else
        emr->rclBounds = empty_bounds;

    ret = emfdc_record( emf, &emr->emr );
    if (ret && !emf->path) emfdc_update_bounds( emf, &emr->rclBounds );
    HeapFree( GetProcessHeap(), 0, emr );
    return ret;
}

BOOL EMFDC_ExtFloodFill( DC_ATTR *dc_attr, INT x, INT y, COLORREF color, UINT fill_type )
{
    EMREXTFLOODFILL emr;

    emr.emr.iType = EMR_EXTFLOODFILL;
    emr.emr.nSize = sizeof(emr);
    emr.ptlStart.x = x;
    emr.ptlStart.y = y;
    emr.crColor = color;
    emr.iMode = fill_type;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_FillRgn( DC_ATTR *dc_attr, HRGN hrgn, HBRUSH hbrush )
{
    struct emf *emf = dc_attr->emf;
    EMRFILLRGN *emr;
    DWORD size, rgnsize, index;
    BOOL ret;

    if (!(index = emfdc_create_brush( emf, hbrush ))) return FALSE;

    rgnsize = NtGdiGetRegionData( hrgn, 0, NULL );
    size = rgnsize + offsetof(EMRFILLRGN,RgnData);
    emr = HeapAlloc( GetProcessHeap(), 0, size );

    NtGdiGetRegionData( hrgn, rgnsize, (RGNDATA *)&emr->RgnData );

    emr->emr.iType = EMR_FILLRGN;
    emr->emr.nSize = size;
    emr->rclBounds.left   = ((RGNDATA *)&emr->RgnData)->rdh.rcBound.left;
    emr->rclBounds.top    = ((RGNDATA *)&emr->RgnData)->rdh.rcBound.top;
    emr->rclBounds.right  = ((RGNDATA *)&emr->RgnData)->rdh.rcBound.right - 1;
    emr->rclBounds.bottom = ((RGNDATA *)&emr->RgnData)->rdh.rcBound.bottom - 1;
    emr->cbRgnData = rgnsize;
    emr->ihBrush = index;

    ret = emfdc_record( emf, &emr->emr );
    if (ret) emfdc_update_bounds( emf, &emr->rclBounds );
    HeapFree( GetProcessHeap(), 0, emr );
    return ret;
}

BOOL EMFDC_FrameRgn( DC_ATTR *dc_attr, HRGN hrgn, HBRUSH hbrush, INT width, INT height )
{
    struct emf *emf = dc_attr->emf;
    EMRFRAMERGN *emr;
    DWORD size, rgnsize, index;
    BOOL ret;

    index = emfdc_create_brush( emf, hbrush );
    if(!index) return FALSE;

    rgnsize = NtGdiGetRegionData( hrgn, 0, NULL );
    size = rgnsize + offsetof(EMRFRAMERGN,RgnData);
    emr = HeapAlloc( GetProcessHeap(), 0, size );

    NtGdiGetRegionData( hrgn, rgnsize, (RGNDATA *)&emr->RgnData );

    emr->emr.iType = EMR_FRAMERGN;
    emr->emr.nSize = size;
    emr->rclBounds.left   = ((RGNDATA *)&emr->RgnData)->rdh.rcBound.left;
    emr->rclBounds.top    = ((RGNDATA *)&emr->RgnData)->rdh.rcBound.top;
    emr->rclBounds.right  = ((RGNDATA *)&emr->RgnData)->rdh.rcBound.right - 1;
    emr->rclBounds.bottom = ((RGNDATA *)&emr->RgnData)->rdh.rcBound.bottom - 1;
    emr->cbRgnData = rgnsize;
    emr->ihBrush = index;
    emr->szlStroke.cx = width;
    emr->szlStroke.cy = height;

    ret = emfdc_record( emf, &emr->emr );
    if (ret) emfdc_update_bounds( emf, &emr->rclBounds );
    HeapFree( GetProcessHeap(), 0, emr );
    return ret;
}

static BOOL emfdc_paint_invert_region( struct emf *emf, HRGN hrgn, DWORD iType )
{
    EMRINVERTRGN *emr;
    DWORD size, rgnsize;
    BOOL ret;

    rgnsize = NtGdiGetRegionData( hrgn, 0, NULL );
    size = rgnsize + offsetof(EMRINVERTRGN,RgnData);
    emr = HeapAlloc( GetProcessHeap(), 0, size );

    NtGdiGetRegionData( hrgn, rgnsize, (RGNDATA *)&emr->RgnData );

    emr->emr.iType = iType;
    emr->emr.nSize = size;
    emr->rclBounds.left   = ((RGNDATA *)&emr->RgnData)->rdh.rcBound.left;
    emr->rclBounds.top    = ((RGNDATA *)&emr->RgnData)->rdh.rcBound.top;
    emr->rclBounds.right  = ((RGNDATA *)&emr->RgnData)->rdh.rcBound.right - 1;
    emr->rclBounds.bottom = ((RGNDATA *)&emr->RgnData)->rdh.rcBound.bottom - 1;
    emr->cbRgnData = rgnsize;

    ret = emfdc_record( emf, &emr->emr );
    if (ret) emfdc_update_bounds( emf, &emr->rclBounds );
    HeapFree( GetProcessHeap(), 0, emr );
    return ret;
}

BOOL EMFDC_PaintRgn( DC_ATTR *dc_attr, HRGN hrgn )
{
    return emfdc_paint_invert_region( dc_attr->emf, hrgn, EMR_PAINTRGN );
}

BOOL EMFDC_InvertRgn( DC_ATTR *dc_attr, HRGN hrgn )
{
    return emfdc_paint_invert_region( dc_attr->emf, hrgn, EMR_INVERTRGN );
}

BOOL EMFDC_ExtTextOut( DC_ATTR *dc_attr, INT x, INT y, UINT flags, const RECT *rect,
                       const WCHAR *str, UINT count, const INT *dx )
{
    struct emf *emf = dc_attr->emf;
    FLOAT ex_scale, ey_scale;
    EMREXTTEXTOUTW *emr;
    int text_height = 0;
    int text_width = 0;
    TEXTMETRICW tm;
    DWORD size;
    BOOL ret;

    size = sizeof(*emr) + ((count+1) & ~1) * sizeof(WCHAR) + count * sizeof(INT);

    TRACE( "%s %s count %d size = %d\n", debugstr_wn(str, count),
           wine_dbgstr_rect(rect), count, size );
    emr = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, size );

    if (dc_attr->graphics_mode == GM_COMPATIBLE)
    {
        const INT horzSize = GetDeviceCaps( dc_attr->hdc, HORZSIZE );
        const INT horzRes  = GetDeviceCaps( dc_attr->hdc, HORZRES );
        const INT vertSize = GetDeviceCaps( dc_attr->hdc, VERTSIZE );
        const INT vertRes  = GetDeviceCaps( dc_attr->hdc, VERTRES );
        SIZE wndext, vportext;

        GetViewportExtEx( dc_attr->hdc, &vportext );
        GetWindowExtEx( dc_attr->hdc, &wndext );
        ex_scale = 100.0 * ((FLOAT)horzSize  / (FLOAT)horzRes) /
            ((FLOAT)wndext.cx / (FLOAT)vportext.cx);
        ey_scale = 100.0 * ((FLOAT)vertSize  / (FLOAT)vertRes) /
            ((FLOAT)wndext.cy / (FLOAT)vportext.cy);
    }
    else
    {
        ex_scale = 0.0;
        ey_scale = 0.0;
    }

    emr->emr.iType = EMR_EXTTEXTOUTW;
    emr->emr.nSize = size;
    emr->iGraphicsMode = dc_attr->graphics_mode;
    emr->exScale = ex_scale;
    emr->eyScale = ey_scale;
    emr->emrtext.ptlReference.x = x;
    emr->emrtext.ptlReference.y = y;
    emr->emrtext.nChars = count;
    emr->emrtext.offString = sizeof(*emr);
    memcpy( (char*)emr + emr->emrtext.offString, str, count * sizeof(WCHAR) );
    emr->emrtext.fOptions = flags;
    if (!rect)
    {
        emr->emrtext.rcl.left = emr->emrtext.rcl.top = 0;
        emr->emrtext.rcl.right = emr->emrtext.rcl.bottom = -1;
    }
    else
    {
        emr->emrtext.rcl.left = rect->left;
        emr->emrtext.rcl.top = rect->top;
        emr->emrtext.rcl.right = rect->right;
        emr->emrtext.rcl.bottom = rect->bottom;
    }

    emr->emrtext.offDx = emr->emrtext.offString + ((count+1) & ~1) * sizeof(WCHAR);
    if (dx)
    {
        UINT i;
        SIZE str_size;
        memcpy( (char*)emr + emr->emrtext.offDx, dx, count * sizeof(INT) );
        for (i = 0; i < count; i++) text_width += dx[i];
        if (GetTextExtentPoint32W( dc_attr->hdc, str, count, &str_size ))
            text_height = str_size.cy;
    }
    else
    {
        UINT i;
        INT *emf_dx = (INT *)((char*)emr + emr->emrtext.offDx);
        SIZE charSize;
        for (i = 0; i < count; i++)
        {
            if (GetTextExtentPoint32W( dc_attr->hdc, str + i, 1, &charSize ))
            {
                emf_dx[i] = charSize.cx;
                text_width += charSize.cx;
                text_height = max( text_height, charSize.cy );
            }
        }
    }

    if (emf->path)
    {
        emr->rclBounds.left = emr->rclBounds.top = 0;
        emr->rclBounds.right = emr->rclBounds.bottom = -1;
        goto no_bounds;
    }

    /* FIXME: handle font escapement */
    switch (dc_attr->text_align & (TA_LEFT | TA_RIGHT | TA_CENTER))
    {
    case TA_CENTER:
        emr->rclBounds.left  = x - (text_width / 2) - 1;
        emr->rclBounds.right = x + (text_width / 2) + 1;
        break;

    case TA_RIGHT:
        emr->rclBounds.left  = x - text_width - 1;
        emr->rclBounds.right = x;
        break;

    default: /* TA_LEFT */
        emr->rclBounds.left  = x;
        emr->rclBounds.right = x + text_width + 1;
    }

    switch (dc_attr->text_align & (TA_TOP | TA_BOTTOM | TA_BASELINE))
    {
    case TA_BASELINE:
        if (!GetTextMetricsW( dc_attr->hdc, &tm )) tm.tmDescent = 0;
        /* Play safe here... it's better to have a bounding box */
        /* that is too big than too small. */
        emr->rclBounds.top    = y - text_height - 1;
        emr->rclBounds.bottom = y + tm.tmDescent + 1;
        break;

    case TA_BOTTOM:
        emr->rclBounds.top    = y - text_height - 1;
        emr->rclBounds.bottom = y;
        break;

    default: /* TA_TOP */
        emr->rclBounds.top    = y;
        emr->rclBounds.bottom = y + text_height + 1;
    }
    emfdc_update_bounds( emf, &emr->rclBounds );

no_bounds:
    ret = emfdc_record( emf, &emr->emr );
    HeapFree( GetProcessHeap(), 0, emr );
    return ret;
}

BOOL EMFDC_GradientFill( DC_ATTR *dc_attr, TRIVERTEX *vert_array, ULONG nvert,
                         void *grad_array, ULONG ngrad, ULONG mode )
{
    EMRGRADIENTFILL *emr;
    ULONG i, pt, size, num_pts = ngrad * (mode == GRADIENT_FILL_TRIANGLE ? 3 : 2);
    const ULONG *pts = (const ULONG *)grad_array;
    BOOL ret;

    size = FIELD_OFFSET(EMRGRADIENTFILL, Ver[nvert]) + num_pts * sizeof(pts[0]);

    emr = HeapAlloc( GetProcessHeap(), 0, size );
    if (!emr) return FALSE;

    for (i = 0; i < num_pts; i++)
    {
        pt = pts[i];

        if (i == 0)
        {
            emr->rclBounds.left = emr->rclBounds.right = vert_array[pt].x;
            emr->rclBounds.top = emr->rclBounds.bottom = vert_array[pt].y;
        }
        else
        {
            if (vert_array[pt].x < emr->rclBounds.left)
                emr->rclBounds.left = vert_array[pt].x;
            else if (vert_array[pt].x > emr->rclBounds.right)
                emr->rclBounds.right = vert_array[pt].x;
            if (vert_array[pt].y < emr->rclBounds.top)
                emr->rclBounds.top = vert_array[pt].y;
            else if (vert_array[pt].y > emr->rclBounds.bottom)
                emr->rclBounds.bottom = vert_array[pt].y;
        }
    }
    emr->rclBounds.right--;
    emr->rclBounds.bottom--;

    emr->emr.iType = EMR_GRADIENTFILL;
    emr->emr.nSize = size;
    emr->nVer = nvert;
    emr->nTri = ngrad;
    emr->ulMode = mode;
    memcpy( emr->Ver, vert_array, nvert * sizeof(vert_array[0]) );
    memcpy( emr->Ver + nvert, pts, num_pts * sizeof(pts[0]) );

    emfdc_update_bounds( dc_attr->emf, &emr->rclBounds );
    ret = emfdc_record( dc_attr->emf, &emr->emr );
    HeapFree( GetProcessHeap(), 0, emr );
    return ret;
}

BOOL EMFDC_FillPath( DC_ATTR *dc_attr )
{
    return emfdrv_stroke_and_fill_path( dc_attr->emf, EMR_FILLPATH );
}

BOOL EMFDC_StrokeAndFillPath( DC_ATTR *dc_attr )
{
    return emfdrv_stroke_and_fill_path( dc_attr->emf, EMR_STROKEANDFILLPATH );
}

BOOL EMFDC_StrokePath( DC_ATTR *dc_attr )
{
    return emfdrv_stroke_and_fill_path( dc_attr->emf, EMR_STROKEPATH );
}

BOOL EMFDC_SetDCBrushColor( DC_ATTR *dc_attr, COLORREF color )
{
    struct emf *emf = dc_attr->emf;
    EMRSELECTOBJECT emr;
    DWORD index;

    if (GetCurrentObject( dc_attr->hdc, OBJ_BRUSH ) != GetStockObject( DC_BRUSH )) return TRUE;

    if (emf->dc_brush) DeleteObject( emf->dc_brush );
    if (!(emf->dc_brush = CreateSolidBrush( color ))) return FALSE;
    if (!(index = emfdc_create_brush( emf, emf->dc_brush ))) return FALSE;
    GDI_hdc_using_object( emf->dc_brush, dc_attr->hdc, emfdc_delete_object );
    emr.emr.iType = EMR_SELECTOBJECT;
    emr.emr.nSize = sizeof(emr);
    emr.ihObject = index;
    return emfdc_record( emf, &emr.emr );
}

BOOL EMFDC_SetDCPenColor( DC_ATTR *dc_attr, COLORREF color )
{
    struct emf *emf = dc_attr->emf;
    EMRSELECTOBJECT emr;
    DWORD index;
    LOGPEN logpen = { PS_SOLID, { 0, 0 }, color };

    if (GetCurrentObject( dc_attr->hdc, OBJ_PEN ) != GetStockObject( DC_PEN )) return TRUE;

    if (emf->dc_pen) DeleteObject( emf->dc_pen );
    if (!(emf->dc_pen = CreatePenIndirect( &logpen ))) return FALSE;
    if (!(index = emfdc_create_pen( emf, emf->dc_pen ))) return FALSE;
    GDI_hdc_using_object( emf->dc_pen, dc_attr->hdc, emfdc_delete_object );
    emr.emr.iType = EMR_SELECTOBJECT;
    emr.emr.nSize = sizeof(emr);
    emr.ihObject = index;
    return emfdc_record( emf, &emr.emr );
}

BOOL EMFDC_SaveDC( DC_ATTR *dc_attr )
{
    EMRSAVEDC emr;

    emr.emr.iType = EMR_SAVEDC;
    emr.emr.nSize = sizeof(emr);
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_RestoreDC( DC_ATTR *dc_attr, INT level )
{
    EMRRESTOREDC emr;

    if (abs(level) > dc_attr->save_level || level == 0) return FALSE;

    emr.emr.iType = EMR_RESTOREDC;
    emr.emr.nSize = sizeof(emr);
    if (level < 0)
        emr.iRelative = level;
    else
        emr.iRelative = level - dc_attr->save_level - 1;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetTextAlign( DC_ATTR *dc_attr, UINT align )
{
    EMRSETTEXTALIGN emr;

    emr.emr.iType = EMR_SETTEXTALIGN;
    emr.emr.nSize = sizeof(emr);
    emr.iMode = align;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetTextJustification( DC_ATTR *dc_attr, INT extra, INT breaks )
{
    EMRSETTEXTJUSTIFICATION emr;

    emr.emr.iType = EMR_SETTEXTJUSTIFICATION;
    emr.emr.nSize = sizeof(emr);
    emr.nBreakExtra = extra;
    emr.nBreakCount = breaks;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetBkMode( DC_ATTR *dc_attr, INT mode )
{
    EMRSETBKMODE emr;

    emr.emr.iType = EMR_SETBKMODE;
    emr.emr.nSize = sizeof(emr);
    emr.iMode = mode;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetBkColor( DC_ATTR *dc_attr, COLORREF color )
{
    EMRSETBKCOLOR emr;

    emr.emr.iType = EMR_SETBKCOLOR;
    emr.emr.nSize = sizeof(emr);
    emr.crColor = color;
    return emfdc_record( dc_attr->emf, &emr.emr );
}


BOOL EMFDC_SetTextColor( DC_ATTR *dc_attr, COLORREF color )
{
    EMRSETTEXTCOLOR emr;

    emr.emr.iType = EMR_SETTEXTCOLOR;
    emr.emr.nSize = sizeof(emr);
    emr.crColor = color;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetROP2( DC_ATTR *dc_attr, INT rop )
{
    EMRSETROP2 emr;

    emr.emr.iType = EMR_SETROP2;
    emr.emr.nSize = sizeof(emr);
    emr.iMode = rop;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetPolyFillMode( DC_ATTR *dc_attr, INT mode )
{
    EMRSETPOLYFILLMODE emr;

    emr.emr.iType = EMR_SETPOLYFILLMODE;
    emr.emr.nSize = sizeof(emr);
    emr.iMode = mode;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetStretchBltMode( DC_ATTR *dc_attr, INT mode )
{
    EMRSETSTRETCHBLTMODE emr;

    emr.emr.iType = EMR_SETSTRETCHBLTMODE;
    emr.emr.nSize = sizeof(emr);
    emr.iMode = mode;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetArcDirection( DC_ATTR *dc_attr, INT dir )
{
    EMRSETARCDIRECTION emr;

    emr.emr.iType = EMR_SETARCDIRECTION;
    emr.emr.nSize = sizeof(emr);
    emr.iArcDirection = dir;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

INT EMFDC_ExcludeClipRect( DC_ATTR *dc_attr, INT left, INT top, INT right, INT bottom )
{
    EMREXCLUDECLIPRECT emr;

    emr.emr.iType      = EMR_EXCLUDECLIPRECT;
    emr.emr.nSize      = sizeof(emr);
    emr.rclClip.left   = left;
    emr.rclClip.top    = top;
    emr.rclClip.right  = right;
    emr.rclClip.bottom = bottom;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_IntersectClipRect( DC_ATTR *dc_attr, INT left, INT top, INT right, INT bottom)
{
    EMRINTERSECTCLIPRECT emr;

    emr.emr.iType      = EMR_INTERSECTCLIPRECT;
    emr.emr.nSize      = sizeof(emr);
    emr.rclClip.left   = left;
    emr.rclClip.top    = top;
    emr.rclClip.right  = right;
    emr.rclClip.bottom = bottom;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_OffsetClipRgn( DC_ATTR *dc_attr, INT x, INT y )
{
    EMROFFSETCLIPRGN emr;

    emr.emr.iType   = EMR_OFFSETCLIPRGN;
    emr.emr.nSize   = sizeof(emr);
    emr.ptlOffset.x = x;
    emr.ptlOffset.y = y;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_ExtSelectClipRgn( DC_ATTR *dc_attr, HRGN hrgn, INT mode )
{
    EMREXTSELECTCLIPRGN *emr;
    DWORD size, rgnsize;
    BOOL ret;

    if (!hrgn)
    {
        if (mode != RGN_COPY) return ERROR;
        rgnsize = 0;
    }
    else rgnsize = NtGdiGetRegionData( hrgn, 0, NULL );

    size = rgnsize + offsetof(EMREXTSELECTCLIPRGN,RgnData);
    emr = HeapAlloc( GetProcessHeap(), 0, size );
    if (rgnsize) NtGdiGetRegionData( hrgn, rgnsize, (RGNDATA *)&emr->RgnData );

    emr->emr.iType = EMR_EXTSELECTCLIPRGN;
    emr->emr.nSize = size;
    emr->cbRgnData = rgnsize;
    emr->iMode     = mode;

    ret = emfdc_record( dc_attr->emf, &emr->emr );
    HeapFree( GetProcessHeap(), 0, emr );
    return ret;
}

BOOL EMFDC_SetMapMode( DC_ATTR *dc_attr, INT mode )
{
    EMRSETMAPMODE emr;

    emr.emr.iType = EMR_SETMAPMODE;
    emr.emr.nSize = sizeof(emr);
    emr.iMode = mode;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetViewportExtEx( DC_ATTR *dc_attr, INT cx, INT cy )
{
    EMRSETVIEWPORTEXTEX emr;

    emr.emr.iType = EMR_SETVIEWPORTEXTEX;
    emr.emr.nSize = sizeof(emr);
    emr.szlExtent.cx = cx;
    emr.szlExtent.cy = cy;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetWindowExtEx( DC_ATTR *dc_attr, INT cx, INT cy )
{
    EMRSETWINDOWEXTEX emr;

    emr.emr.iType = EMR_SETWINDOWEXTEX;
    emr.emr.nSize = sizeof(emr);
    emr.szlExtent.cx = cx;
    emr.szlExtent.cy = cy;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetViewportOrgEx( DC_ATTR *dc_attr, INT x, INT y )
{
    EMRSETVIEWPORTORGEX emr;

    emr.emr.iType = EMR_SETVIEWPORTORGEX;
    emr.emr.nSize = sizeof(emr);
    emr.ptlOrigin.x = x;
    emr.ptlOrigin.y = y;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetWindowOrgEx( DC_ATTR *dc_attr, INT x, INT y )
{
    EMRSETWINDOWORGEX emr;

    emr.emr.iType = EMR_SETWINDOWORGEX;
    emr.emr.nSize = sizeof(emr);
    emr.ptlOrigin.x = x;
    emr.ptlOrigin.y = y;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_ScaleViewportExtEx( DC_ATTR *dc_attr, INT x_num, INT x_denom, INT y_num, INT y_denom )
{
    EMRSCALEVIEWPORTEXTEX emr;

    emr.emr.iType = EMR_SCALEVIEWPORTEXTEX;
    emr.emr.nSize = sizeof(emr);
    emr.xNum      = x_num;
    emr.xDenom    = x_denom;
    emr.yNum      = y_num;
    emr.yDenom    = y_denom;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_ScaleWindowExtEx( DC_ATTR *dc_attr, INT x_num, INT x_denom, INT y_num, INT y_denom )
{
    EMRSCALEWINDOWEXTEX emr;

    emr.emr.iType = EMR_SCALEWINDOWEXTEX;
    emr.emr.nSize = sizeof(emr);
    emr.xNum      = x_num;
    emr.xDenom    = x_denom;
    emr.yNum      = y_num;
    emr.yDenom    = y_denom;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetLayout( DC_ATTR *dc_attr, DWORD layout )
{
    EMRSETLAYOUT emr;

    emr.emr.iType = EMR_SETLAYOUT;
    emr.emr.nSize = sizeof(emr);
    emr.iMode = layout;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetWorldTransform( DC_ATTR *dc_attr, const XFORM *xform )
{
    EMRSETWORLDTRANSFORM emr;

    emr.emr.iType = EMR_SETWORLDTRANSFORM;
    emr.emr.nSize = sizeof(emr);
    emr.xform = *xform;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_ModifyWorldTransform( DC_ATTR *dc_attr, const XFORM *xform, DWORD mode )
{
    EMRMODIFYWORLDTRANSFORM emr;

    emr.emr.iType = EMR_MODIFYWORLDTRANSFORM;
    emr.emr.nSize = sizeof(emr);
    if (mode == MWT_IDENTITY)
    {
        emr.xform.eM11 = 1.0f;
        emr.xform.eM12 = 0.0f;
        emr.xform.eM21 = 0.0f;
        emr.xform.eM22 = 1.0f;
        emr.xform.eDx = 0.0f;
        emr.xform.eDy = 0.0f;
    }
    else
    {
        emr.xform = *xform;
    }
    emr.iMode = mode;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SetMapperFlags( DC_ATTR *dc_attr, DWORD flags )
{
    EMRSETMAPPERFLAGS emr;

    emr.emr.iType = EMR_SETMAPPERFLAGS;
    emr.emr.nSize = sizeof(emr);
    emr.dwFlags   = flags;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_AbortPath( DC_ATTR *dc_attr )
{
    struct emf *emf = dc_attr->emf;
    EMRABORTPATH emr;

    emr.emr.iType = EMR_ABORTPATH;
    emr.emr.nSize = sizeof(emr);

    emf->path = FALSE;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_BeginPath( DC_ATTR *dc_attr )
{
    struct emf *emf = dc_attr->emf;
    EMRBEGINPATH emr;

    emr.emr.iType = EMR_BEGINPATH;
    emr.emr.nSize = sizeof(emr);
    if (!emfdc_record( emf, &emr.emr )) return FALSE;

    emf->path = TRUE;
    return TRUE;
}

BOOL EMFDC_CloseFigure( DC_ATTR *dc_attr )
{
    EMRCLOSEFIGURE emr;

    emr.emr.iType = EMR_CLOSEFIGURE;
    emr.emr.nSize = sizeof(emr);
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_EndPath( DC_ATTR *dc_attr )
{
    struct emf *emf = dc_attr->emf;
    EMRENDPATH emr;

    emf->path = FALSE;

    emr.emr.iType = EMR_ENDPATH;
    emr.emr.nSize = sizeof(emr);
    return emfdc_record( emf, &emr.emr );
}

BOOL EMFDC_FlattenPath( DC_ATTR *dc_attr )
{
    EMRFLATTENPATH emr;

    emr.emr.iType = EMR_FLATTENPATH;
    emr.emr.nSize = sizeof(emr);
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_SelectClipPath( DC_ATTR *dc_attr, INT mode )
{
    EMRSELECTCLIPPATH emr;

    emr.emr.iType = EMR_SELECTCLIPPATH;
    emr.emr.nSize = sizeof(emr);
    emr.iMode = mode;
    return emfdc_record( dc_attr->emf, &emr.emr );
}

BOOL EMFDC_WidenPath( DC_ATTR *dc_attr )
{
    EMRWIDENPATH emr;

    emr.emr.iType = EMR_WIDENPATH;
    emr.emr.nSize = sizeof(emr);
    return emfdc_record( dc_attr->emf, &emr.emr );
}

void EMFDC_DeleteDC( DC_ATTR *dc_attr )
{
    struct emf *emf = dc_attr->emf;
    UINT index;

    HeapFree( GetProcessHeap(), 0, emf->emh );
    for (index = 0; index < emf->handles_size; index++)
        if (emf->handles[index])
            GDI_hdc_not_using_object( emf->handles[index], dc_attr->hdc );
    HeapFree( GetProcessHeap(), 0, emf->handles );
}

/*******************************************************************
 *      GdiComment   (GDI32.@)
 */
BOOL WINAPI GdiComment( HDC hdc, UINT bytes, const BYTE *buffer )
{
    DC_ATTR *dc_attr;
    EMRGDICOMMENT *emr;
    UINT total, rounded_size;
    BOOL ret;

    if (!(dc_attr = get_dc_attr( hdc )) || !dc_attr->emf) return FALSE;

    rounded_size = (bytes+3) & ~3;
    total = offsetof(EMRGDICOMMENT,Data) + rounded_size;

    emr = HeapAlloc(GetProcessHeap(), 0, total);
    emr->emr.iType = EMR_GDICOMMENT;
    emr->emr.nSize = total;
    emr->cbData = bytes;
    memset(&emr->Data[bytes], 0, rounded_size - bytes);
    memcpy(&emr->Data[0], buffer, bytes);

    ret = emfdc_record( dc_attr->emf, &emr->emr );

    HeapFree(GetProcessHeap(), 0, emr);

    return ret;
}

/**********************************************************************
 *           CreateEnhMetaFileA   (GDI32.@)
 */
HDC WINAPI CreateEnhMetaFileA( HDC hdc, const char *filename, const RECT *rect,
                               const char *description )
{
    WCHAR *filenameW = NULL;
    WCHAR *descriptionW = NULL;
    DWORD len1, len2, total;
    HDC ret;

    if (filename)
    {
        total = MultiByteToWideChar( CP_ACP, 0, filename, -1, NULL, 0 );
        filenameW = HeapAlloc( GetProcessHeap(), 0, total * sizeof(WCHAR) );
        MultiByteToWideChar( CP_ACP, 0, filename, -1, filenameW, total );
    }

    if(description)
    {
        len1 = strlen(description);
        len2 = strlen(description + len1 + 1);
        total = MultiByteToWideChar( CP_ACP, 0, description, len1 + len2 + 3, NULL, 0 );
        descriptionW = HeapAlloc( GetProcessHeap(), 0, total * sizeof(WCHAR) );
        MultiByteToWideChar( CP_ACP, 0, description, len1 + len2 + 3, descriptionW, total );
    }

    ret = CreateEnhMetaFileW( hdc, filenameW, rect, descriptionW );

    HeapFree( GetProcessHeap(), 0, filenameW );
    HeapFree( GetProcessHeap(), 0, descriptionW );
    return ret;
}

/**********************************************************************
 *           CreateEnhMetaFileW   (GDI32.@)
 */
HDC WINAPI CreateEnhMetaFileW( HDC hdc, const WCHAR *filename, const RECT *rect,
                               const WCHAR *description )
{
    HDC ret;
    struct emf *emf;
    DC_ATTR *dc_attr;
    HANDLE file;
    DWORD size = 0, length = 0;

    TRACE( "(%p %s %s %s)\n", hdc, debugstr_w(filename), wine_dbgstr_rect(rect),
           debugstr_w(description) );

    if (!(ret = NtGdiCreateMetafileDC( hdc ))) return 0;

    if (!(dc_attr = get_dc_attr( ret )) || !(emf = HeapAlloc( GetProcessHeap(), 0, sizeof(*emf) )))
    {
        DeleteDC( ret );
        return 0;
    }

    dc_attr->emf = emf;

    if (description) /* App name\0Title\0\0 */
    {
        length = lstrlenW( description );
        length += lstrlenW( description + length + 1 );
        length += 3;
        length *= 2;
    }
    size = sizeof(ENHMETAHEADER) + (length + 3) / 4 * 4;

    if (!(emf->emh = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, size)))
    {
        DeleteDC( ret );
        return 0;
    }
    emf->dc_attr = dc_attr;

    emf->handles = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY,
                              HANDLE_LIST_INC * sizeof(emf->handles[0]) );
    emf->handles_size = HANDLE_LIST_INC;
    emf->cur_handles = 1;
    emf->hFile = 0;
    emf->dc_brush = 0;
    emf->dc_pen = 0;
    emf->path = FALSE;

    emf->emh->iType = EMR_HEADER;
    emf->emh->nSize = size;

    dc_attr->emf_bounds.left = dc_attr->emf_bounds.top = 0;
    dc_attr->emf_bounds.right = dc_attr->emf_bounds.bottom = -1;

    if (rect)
    {
        emf->emh->rclFrame.left   = rect->left;
        emf->emh->rclFrame.top    = rect->top;
        emf->emh->rclFrame.right  = rect->right;
        emf->emh->rclFrame.bottom = rect->bottom;
    }
    else
    {
        /* Set this to {0,0 - -1,-1} and update it at the end */
        emf->emh->rclFrame.left = emf->emh->rclFrame.top = 0;
        emf->emh->rclFrame.right = emf->emh->rclFrame.bottom = -1;
    }

    emf->emh->dSignature = ENHMETA_SIGNATURE;
    emf->emh->nVersion = 0x10000;
    emf->emh->nBytes = emf->emh->nSize;
    emf->emh->nRecords = 1;
    emf->emh->nHandles = 1;

    emf->emh->sReserved = 0; /* According to docs, this is reserved and must be 0 */
    emf->emh->nDescription = length / 2;

    emf->emh->offDescription = length ? sizeof(ENHMETAHEADER) : 0;

    emf->emh->nPalEntries = 0; /* I guess this should start at 0 */

    /* Size in pixels */
    emf->emh->szlDevice.cx = GetDeviceCaps( ret, HORZRES );
    emf->emh->szlDevice.cy = GetDeviceCaps( ret, VERTRES );

    /* Size in millimeters */
    emf->emh->szlMillimeters.cx = GetDeviceCaps( ret, HORZSIZE );
    emf->emh->szlMillimeters.cy = GetDeviceCaps( ret, VERTSIZE );

    /* Size in micrometers */
    emf->emh->szlMicrometers.cx = emf->emh->szlMillimeters.cx * 1000;
    emf->emh->szlMicrometers.cy = emf->emh->szlMillimeters.cy * 1000;

    memcpy( (char *)emf->emh + sizeof(ENHMETAHEADER), description, length );

    if (filename)  /* disk based metafile */
    {
        if ((file = CreateFileW( filename, GENERIC_WRITE | GENERIC_READ, 0,
                                  NULL, CREATE_ALWAYS, 0, 0)) == INVALID_HANDLE_VALUE)
        {
            DeleteDC( ret );
            return 0;
        }
        emf->hFile = file;
    }

    TRACE( "returning %p\n", ret );
    return ret;
}

/******************************************************************
 *           CloseEnhMetaFile (GDI32.@)
 */
HENHMETAFILE WINAPI CloseEnhMetaFile( HDC hdc )
{
    HENHMETAFILE hmf;
    struct emf *emf;
    DC_ATTR *dc_attr;
    EMREOF emr;
    HANDLE mapping = 0;

    TRACE("(%p)\n", hdc );

    if (!(dc_attr = get_dc_attr( hdc )) || !dc_attr->emf) return 0;
    emf = dc_attr->emf;

    if (dc_attr->save_level)
        RestoreDC( hdc, 1 );

    if (emf->dc_brush) DeleteObject( emf->dc_brush );
    if (emf->dc_pen) DeleteObject( emf->dc_pen );

    emr.emr.iType = EMR_EOF;
    emr.emr.nSize = sizeof(emr);
    emr.nPalEntries = 0;
    emr.offPalEntries = FIELD_OFFSET(EMREOF, nSizeLast);
    emr.nSizeLast = emr.emr.nSize;
    emfdc_record( emf, &emr.emr );

    emf->emh->rclBounds = dc_attr->emf_bounds;

    /* Update rclFrame if not initialized in CreateEnhMetaFile */
    if (emf->emh->rclFrame.left > emf->emh->rclFrame.right)
    {
        emf->emh->rclFrame.left = emf->emh->rclBounds.left *
            emf->emh->szlMillimeters.cx * 100 / emf->emh->szlDevice.cx;
        emf->emh->rclFrame.top = emf->emh->rclBounds.top *
            emf->emh->szlMillimeters.cy * 100 / emf->emh->szlDevice.cy;
        emf->emh->rclFrame.right = emf->emh->rclBounds.right *
            emf->emh->szlMillimeters.cx * 100 / emf->emh->szlDevice.cx;
        emf->emh->rclFrame.bottom = emf->emh->rclBounds.bottom *
            emf->emh->szlMillimeters.cy * 100 / emf->emh->szlDevice.cy;
    }

    if (emf->hFile)  /* disk based metafile */
    {
        if (!WriteFile( emf->hFile, emf->emh, emf->emh->nBytes, NULL, NULL ))
        {
            CloseHandle( emf->hFile );
            return 0;
        }
        HeapFree( GetProcessHeap(), 0, emf->emh );
        mapping = CreateFileMappingA( emf->hFile, NULL, PAGE_READONLY, 0, 0, NULL );
        TRACE( "mapping = %p\n", mapping );
        emf->emh = MapViewOfFile( mapping, FILE_MAP_READ, 0, 0, 0 );
        TRACE( "view = %p\n", emf->emh );
        CloseHandle( mapping );
        CloseHandle( emf->hFile );
    }

    hmf = EMF_Create_HENHMETAFILE( emf->emh, emf->emh->nBytes, emf->hFile != 0 );
    emf->emh = NULL;  /* So it won't be deleted */
    DeleteDC( hdc );
    return hmf;
}