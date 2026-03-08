/******************************************************************************
 *   Copyright 2015 Broadcom Corporation
 *
 * This program is the proprietary software of Broadcom Corporation and/or its
 * licensors, and may only be used, duplicated, modified or distributed
 * pursuant to the terms and conditions of a separate, written license
 * agreement executed between you and Broadcom (an "Authorized License").
 * Except as set forth in an Authorized License, Broadcom grants no license
 * (express or implied), right to use, or waiver of any kind with respect to
 * the Software, and Broadcom expressly reserves all rights in and to the
 * Software and all intellectual property rights therein.  IF YOU HAVE NO
 * AUTHORIZED LICENSE, THEN YOU HAVE NO RIGHT TO USE THIS SOFTWARE IN ANY WAY,
 * AND SHOULD IMMEDIATELY NOTIFY BROADCOM AND DISCONTINUE ALL USE OF THE
 * SOFTWARE.
 *
 * Except as expressly set forth in the Authorized License,
 *
 * 1.     This program, including its structure, sequence and organization,
 * constitutes the valuable trade secrets of Broadcom, and you shall use all
 * reasonable efforts to protect the confidentiality thereof, and to use this
 * information only in connection with your use of Broadcom integrated circuit
 * products.
 *
 * 2.     TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED
 * "AS IS" AND WITH ALL FAULTS AND BROADCOM MAKES NO PROMISES, REPRESENTATIONS
 * OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH
 * RESPECT TO THE SOFTWARE.  BROADCOM SPECIFICALLY DISCLAIMS ANY AND ALL
 * IMPLIED WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR
 * A PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET
 * ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. YOU ASSUME THE
 * ENTIRE RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE.
 *
 * 3.     TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL BROADCOM OR
 * ITS LICENSORS BE LIABLE FOR (i) CONSEQUENTIAL, INCIDENTAL, SPECIAL,
 * INDIRECT, OR EXEMPLARY DAMAGES WHATSOEVER ARISING OUT OF OR IN ANY WAY
 * RELATING TO YOUR USE OF OR INABILITY TO USE THE SOFTWARE EVEN IF BROADCOM
 * HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES; OR (ii) ANY AMOUNT IN
 * EXCESS OF THE AMOUNT ACTUALLY PAID FOR THE SOFTWARE ITSELF OR U.S. $1,
 * WHICHEVER IS GREATER. THESE LIMITATIONS SHALL APPLY NOTWITHSTANDING ANY
 * FAILURE OF ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.
 *****************************************************************************/

#include "cairoint.h"
#include "cairo-nexus.h"

#include "cairo-clip-private.h"
#include "cairo-compositor-private.h"
#include "cairo-default-context-private.h"
#include "cairo-error-private.h"
#include "cairo-image-surface-inline.h"
#include "cairo-pattern-private.h"
#include "cairo-surface-backend-private.h"
#include "cairo-surface-fallback-private.h"

#include <pixman.h>

slim_hidden_proto(cairo_nexus_surface_create);

typedef struct _cairo_nexus_surface {
    cairo_image_surface_t image;
    CNexus *nexus;
    CNexusSurface *nexusSurface;
} cairo_nexus_surface_t;

/* XXX: A1 has a different bits ordering in cairo.
 *      Probably we should drop it.
 */
#define NEXUS_COLOR_ALPHA(a) \
    (((a) == NEXUS_PixelFormat_eA8_R8_G8_B8) || \
     ((a) == NEXUS_PixelFormat_eA8_B8_G8_R8) || \
     ((a) == NEXUS_PixelFormat_eR8_G8_B8_A8) || \
     ((a) == NEXUS_PixelFormat_eB8_G8_R8_A8) || \
     ((a) == NEXUS_PixelFormat_eA8_R8_G8_B8) || \
     ((a) == NEXUS_PixelFormat_eA8_R8_G8_B8))

//TODO: validate the mapping between cairo and nexus color
static cairo_content_t _nexus_format_to_content(NEXUS_PixelFormat format) {
#if 0
    if (NEXUS_PIXEL_FORMAT_IS_PALETTE(format)) {
        if (NEXUS_COLOR_ALPHA(format))
            return CAIRO_CONTENT_COLOR_ALPHA;

        return CAIRO_CONTENT_ALPHA;
    }
#endif
    return CAIRO_CONTENT_COLOR_ALPHA;//CAIRO_CONTENT_COLOR;
}

static inline NEXUS_PixelFormat _cairo_to_nexus_format(cairo_format_t format) {
    switch (format) {

    case CAIRO_FORMAT_RGB24:
        return NEXUS_PixelFormat_eX8_R8_G8_B8;
    case CAIRO_FORMAT_ARGB32:
        return NEXUS_PixelFormat_eA8_R8_G8_B8;
    case CAIRO_FORMAT_A8:
        return NEXUS_PixelFormat_eA8;
    case CAIRO_FORMAT_A1:
        return NEXUS_PixelFormat_eA1;
    case CAIRO_FORMAT_INVALID:
    case CAIRO_FORMAT_RGB16_565:
    case CAIRO_FORMAT_RGB30:
    default:
        break;
    }
    return -1;
}

static inline pixman_format_code_t _nexus_to_pixman_format(
        NEXUS_PixelFormat format) {
    switch (format) {
   case NEXUS_PixelFormat_eA1_R5_G5_B5:
        return PIXMAN_a1r5g5b5;
    case NEXUS_PixelFormat_eR5_G6_B5:
        return PIXMAN_r5g6b5;
    case NEXUS_PixelFormat_eR8_G8_B8:
        return PIXMAN_r8g8b8;
    case NEXUS_PixelFormat_eX8_R8_G8_B8:
        return PIXMAN_x8r8g8b8;
    case NEXUS_PixelFormat_eA8_R8_G8_B8:
        return PIXMAN_a8r8g8b8;
    case NEXUS_PixelFormat_eA8:
        return PIXMAN_a8;
    case NEXUS_PixelFormat_eA1:
        return PIXMAN_a1; /* bit reversed, oops */
    case NEXUS_PixelFormat_eA4_R4_G4_B4:
        return PIXMAN_a4r4g4b4;
    case NEXUS_PixelFormat_eA4:
        return PIXMAN_a4;
    case NEXUS_PixelFormat_eX4_R4_G4_B4:
        return PIXMAN_x4r4g4b4;
    case NEXUS_PixelFormat_eX1_R5_G5_B5:
        return PIXMAN_x1r5g5b5;
    case NEXUS_PixelFormat_eX1_B5_G5_R5:
        return PIXMAN_x1b5g5r5;
    case NEXUS_PixelFormat_eUnknown:
    case NEXUS_PixelFormat_eMax:
    case NEXUS_PixelFormat_eB5_G6_R5:
    case NEXUS_PixelFormat_eA1_B5_G5_R5:
    case NEXUS_PixelFormat_eR5_G5_B5_A1:
    case NEXUS_PixelFormat_eR5_G5_B5_X1:
    case NEXUS_PixelFormat_eB5_G5_R5_A1:
    case NEXUS_PixelFormat_eB5_G5_R5_X1:
    case NEXUS_PixelFormat_eA4_B4_G4_R4:
    case NEXUS_PixelFormat_eX4_B4_G4_R4:
    case NEXUS_PixelFormat_eR4_G4_B4_A4:
    case NEXUS_PixelFormat_eR4_G4_B4_X4:
    case NEXUS_PixelFormat_eB4_G4_R4_A4:
    case NEXUS_PixelFormat_eB4_G4_R4_X4:
    case NEXUS_PixelFormat_eA8_B8_G8_R8:
    case NEXUS_PixelFormat_eX8_B8_G8_R8:
    case NEXUS_PixelFormat_eR8_G8_B8_A8:
    case NEXUS_PixelFormat_eR8_G8_B8_X8:
    case NEXUS_PixelFormat_eB8_G8_R8_A8:
    case NEXUS_PixelFormat_eB8_G8_R8_X8:
    case NEXUS_PixelFormat_eA2:
    case NEXUS_PixelFormat_eW1:
    case NEXUS_PixelFormat_eA8_Palette8:
    case NEXUS_PixelFormat_ePalette8:
    case NEXUS_PixelFormat_ePalette4:
    case NEXUS_PixelFormat_ePalette2:
    case NEXUS_PixelFormat_ePalette1:
    case NEXUS_PixelFormat_eY8_Palette8:
    case NEXUS_PixelFormat_eA8_Y8:
    case NEXUS_PixelFormat_eCb8:
    case NEXUS_PixelFormat_eCr8:
    case NEXUS_PixelFormat_eY8:
    case NEXUS_PixelFormat_eCb8_Cr8:
    case NEXUS_PixelFormat_eCr8_Cb8:
    case NEXUS_PixelFormat_eY10:
    case NEXUS_PixelFormat_eCb10_Cr10:
    case NEXUS_PixelFormat_eCr10_Cb10:
    case NEXUS_PixelFormat_eY08_Cb8_Y18_Cr8:
    case NEXUS_PixelFormat_eY08_Cr8_Y18_Cb8:
    case NEXUS_PixelFormat_eY18_Cb8_Y08_Cr8:
    case NEXUS_PixelFormat_eY18_Cr8_Y08_Cb8:
    case NEXUS_PixelFormat_eCb8_Y08_Cr8_Y18:
    case NEXUS_PixelFormat_eCb8_Y18_Cr8_Y08:
    case NEXUS_PixelFormat_eCr8_Y18_Cb8_Y08:
    case NEXUS_PixelFormat_eCr8_Y08_Cb8_Y18:
    case NEXUS_PixelFormat_eX2_Cr10_Y10_Cb10:
    case NEXUS_PixelFormat_eA8_Y8_Cb8_Cr8:
    case NEXUS_PixelFormat_eA8_Cr8_Cb8_Y8:
    case NEXUS_PixelFormat_eCr8_Cb8_Y8_A8:
    case NEXUS_PixelFormat_eY8_Cb8_Cr8_A8:
    case NEXUS_PixelFormat_eY010_Cb10_Y110_Cr10:
    case NEXUS_PixelFormat_eY010_Cr10_Y110_Cb10:
    case NEXUS_PixelFormat_eY110_Cb10_Y010_Cr10:
    case NEXUS_PixelFormat_eY110_Cr10_Y010_Cb10:
    case NEXUS_PixelFormat_eCb10_Y010_Cr10_Y110:
    case NEXUS_PixelFormat_eCb10_Y110_Cr10_Y010:
    case NEXUS_PixelFormat_eCr10_Y110_Cb10_Y010:
    case NEXUS_PixelFormat_eCr10_Y010_Cb10_Y110:
    case NEXUS_PixelFormat_eL8:
    case NEXUS_PixelFormat_eL8_A8:
    case NEXUS_PixelFormat_eL15_L05_A6:
    case NEXUS_PixelFormat_eL4_A4:
    case NEXUS_PixelFormat_eYCbCr422_10bit:
    default:
        break;
    }
    return 0;
}

static cairo_surface_t *
_cairo_nexus_surface_create_similar (void            *abstract_src,
                   cairo_content_t  content,
                   int              width,
                   int              height)
{
    cairo_nexus_surface_t *other = abstract_src;
    NEXUS_PixelFormat format;
    CNexusSurface *nexusSurface;
    cairo_surface_t *surface;

    if (width <= 0 || height <= 0)
    return _cairo_image_surface_create_with_content (content, width, height);

    format = _cairo_to_nexus_format (_cairo_format_from_content (content));

    if (other->nexus->createSurface(other->nexus, format, width, height, &nexusSurface) != NEXUS_SUCCESS)
    return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_DEVICE_ERROR));

    surface = cairo_nexus_surface_create (other->nexus, nexusSurface);
    nexusSurface->release (nexusSurface);

    return surface;
}

static cairo_status_t
_cairo_nexus_surface_finish (void *abstract_surface)
{
    cairo_nexus_surface_t *surface = abstract_surface;

    if (surface->nexusSurface) {
        surface->nexusSurface->release (surface->nexusSurface);
        surface->nexusSurface = NULL;
    }
    if (surface->nexus) {
        surface->nexus = NULL;
    }
    return _cairo_image_surface_finish (abstract_surface);
}

static cairo_image_surface_t *
_cairo_nexus_surface_map_to_image (void *abstract_surface,
                 const cairo_rectangle_int_t *extents)
{
    cairo_nexus_surface_t *surface = abstract_surface;
    return _cairo_image_surface_map_to_image (&surface->image.base, extents);
}

static cairo_int_status_t
_cairo_nexus_surface_unmap_image (void *abstract_surface,
                cairo_image_surface_t *image)
{
    cairo_nexus_surface_t *surface = abstract_surface;
    return _cairo_image_surface_unmap_image (&surface->image.base, image);
}

static cairo_status_t
_cairo_nexus_surface_flush (void *abstract_surface,
              unsigned flags)
{
    cairo_nexus_surface_t *surface = abstract_surface;

    if (flags)
    return CAIRO_STATUS_SUCCESS;

    surface->nexusSurface->sync(surface->nexusSurface);

    return CAIRO_STATUS_SUCCESS;
}

static inline NEXUS_PixelFormat _nexus_from_pixman_format(
        pixman_format_code_t format) {
    switch ((int) format) {
    case PIXMAN_a1r5g5b5:
        return NEXUS_PixelFormat_eA1_R5_G5_B5;
    case PIXMAN_r5g6b5:
        return NEXUS_PixelFormat_eR5_G6_B5;
    case PIXMAN_r8g8b8:
        return NEXUS_PixelFormat_eR8_G8_B8;
    case PIXMAN_x8r8g8b8:
        return NEXUS_PixelFormat_eX8_R8_G8_B8;
    case PIXMAN_a8r8g8b8:
        return NEXUS_PixelFormat_eA8_R8_G8_B8;
    case PIXMAN_a8:
        return NEXUS_PixelFormat_eA8;
//    case PIXMAN_yuy2: return DSPF_YUY2;
//    case PIXMAN_r3g3b2: return DSPF_RGB332;
//    case PIXMAN_yv12: return DSPF_YV12;
    case PIXMAN_a1:
        return NEXUS_PixelFormat_eA1; /* bit reversed, oops */
    case PIXMAN_a4r4g4b4:
        return NEXUS_PixelFormat_eA4_R4_G4_B4;
    case PIXMAN_a4:
        return NEXUS_PixelFormat_eA4;
    case PIXMAN_x4r4g4b4:
        return NEXUS_PixelFormat_eX4_R4_G4_B4;
    case PIXMAN_x1r5g5b5:
        return NEXUS_PixelFormat_eX1_R5_G5_B5;
    case PIXMAN_x1b5g5r5:
        return NEXUS_PixelFormat_eX1_B5_G5_R5;
    default:
        return 0;
    }
}

static cairo_bool_t
_nexus_get_operator (cairo_operator_t         operator,
NEXUS_PorterDuffOp *nexusOp) {
    switch (operator) {
    case CAIRO_OPERATOR_CLEAR:
        *nexusOp = NEXUS_PorterDuffOp_eClear;
        break;
    case CAIRO_OPERATOR_SOURCE:
        *nexusOp = NEXUS_PorterDuffOp_eSrc;
        break;
    case CAIRO_OPERATOR_OVER:
        *nexusOp = NEXUS_PorterDuffOp_eSrcOver;
        break;
    case CAIRO_OPERATOR_IN:
        *nexusOp = NEXUS_PorterDuffOp_eSrcIn;
        break;
    case CAIRO_OPERATOR_OUT:
        *nexusOp = NEXUS_PorterDuffOp_eSrcOut;
        break;
    case CAIRO_OPERATOR_ATOP:
        *nexusOp = NEXUS_PorterDuffOp_eSrcAtop;
        break;
    case CAIRO_OPERATOR_DEST:
        *nexusOp = NEXUS_PorterDuffOp_eDst;
        break;
    case CAIRO_OPERATOR_DEST_OVER:
        *nexusOp = NEXUS_PorterDuffOp_eDstOver;
        break;
    case CAIRO_OPERATOR_DEST_IN:
        *nexusOp = NEXUS_PorterDuffOp_eDstIn;
        break;
    case CAIRO_OPERATOR_DEST_OUT:
        *nexusOp = NEXUS_PorterDuffOp_eDstOut;
        break;
    case CAIRO_OPERATOR_DEST_ATOP:
        *nexusOp = NEXUS_PorterDuffOp_eDstAtop;
        break;
    case CAIRO_OPERATOR_XOR:
        *nexusOp = NEXUS_PorterDuffOp_eXor;
        break;
    case CAIRO_OPERATOR_ADD:
    case CAIRO_OPERATOR_SATURATE:
    case CAIRO_OPERATOR_MULTIPLY:
    case CAIRO_OPERATOR_SCREEN:
    case CAIRO_OPERATOR_OVERLAY:
    case CAIRO_OPERATOR_DARKEN:
    case CAIRO_OPERATOR_LIGHTEN:
    case CAIRO_OPERATOR_COLOR_DODGE:
    case CAIRO_OPERATOR_COLOR_BURN:
    case CAIRO_OPERATOR_HARD_LIGHT:
    case CAIRO_OPERATOR_SOFT_LIGHT:
    case CAIRO_OPERATOR_DIFFERENCE:
    case CAIRO_OPERATOR_EXCLUSION:
    case CAIRO_OPERATOR_HSL_HUE:
    case CAIRO_OPERATOR_HSL_SATURATION:
    case CAIRO_OPERATOR_HSL_COLOR:
    case CAIRO_OPERATOR_HSL_LUMINOSITY:
    default:
        *nexusOp = NEXUS_PorterDuffOp_eMax;
        return FALSE;
    }
    return TRUE;
}

static cairo_int_status_t
_cairo_nexus_surface_paint (void *surface,
                            cairo_operator_t op,
                            const cairo_pattern_t *source,
                            const cairo_clip_t *clip)
{
    return CAIRO_INT_STATUS_UNSUPPORTED;
}

static cairo_int_status_t
_cairo_nexus_surface_fill (void *surface,
                           cairo_operator_t op,
                           const cairo_pattern_t *source,
                           const cairo_path_fixed_t *path,
                           cairo_fill_rule_t fill_rule,
                           double tolerance,
                           cairo_antialias_t antialias,
                           const cairo_clip_t *clip)
{
#if 0
    cairo_nexus_surface_t *dst = abstract_surface;
    NEXUS_Rect r[n_rects];
    int i;
    uint8_t colorComponents[4]; //a,r,g,b
    NEXUS_PorterDuffOp  operator;
    CNexusSurface *surface = dst->nexusSurface;

    if (! _nexus_get_operator (op, &operator))
    {
        return CAIRO_INT_STATUS_UNSUPPORTED;
    }

    for (i = 0; i < n_rects; i++) {
        r[i].x = rects[i].x;
        r[i].y = rects[i].y;
        r[i].width = rects[i].width;
        r[i].height = rects[i].height;
    }
    if (1) { //isPreMultiplied
        colorComponents[0] = color->alpha_short >> 8;
        colorComponents[1] = color->red_short >> 8;
        colorComponents[2] = color->green_short >> 8;
        colorComponents[3] = color->blue_short >> 8;
    } else {
        colorComponents[0] = color->alpha * 0xff;
        colorComponents[1] = color->red * 0xff;
        colorComponents[2] = color->green * 0xff;
        colorComponents[3] = color->blue * 0xff;
    }
    surface->fillRectangles(surface, colorComponents, operator, r, n_rects);

    return CAIRO_STATUS_SUCCESS;a
#else
    return CAIRO_INT_STATUS_UNSUPPORTED;
#endif
}

static cairo_surface_backend_t
_cairo_nexus_surface_backend = {
    CAIRO_SURFACE_TYPE_DIRECTFB, /*type*/
    _cairo_nexus_surface_finish, /*finish*/
    _cairo_default_context_create,

    _cairo_nexus_surface_create_similar,/*create_similar*/
    NULL, /* create similar image */
    _cairo_nexus_surface_map_to_image,
    _cairo_nexus_surface_unmap_image,

    _cairo_surface_default_source,
    _cairo_surface_default_acquire_source_image,
    _cairo_surface_default_release_source_image,
    NULL,

    NULL, /* copy_page */
    NULL, /* show_page */

    _cairo_image_surface_get_extents,
    _cairo_image_surface_get_font_options,

    _cairo_nexus_surface_flush,
    NULL, /* mark_dirty_rectangle */

    _cairo_surface_fallback_paint, //_cairo_nexus_surface_paint,
    _cairo_surface_fallback_mask,
    _cairo_surface_fallback_stroke,
    _cairo_surface_fallback_fill, //_cairo_nexus_surface_fill,
    NULL, /* fill-stroke */
    _cairo_surface_fallback_glyphs,
};

cairo_surface_t *
cairo_nexus_surface_create (CNexus *nexus, CNexusSurface *nexusSurface)
{
    cairo_nexus_surface_t *surface;
    NEXUS_PixelFormat format;
    pixman_format_code_t pixman_format;
    NEXUS_SurfaceMemory mem;
    pixman_image_t *image;

    if (! nexus || ! nexusSurface)
        return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_INVALID_FORMAT));

    format = nexusSurface->getPixelFormat(nexusSurface);
    pixman_format = _nexus_to_pixman_format (format);
    if (! pixman_format_supported_destination (pixman_format))
        return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_INVALID_FORMAT));

    surface = calloc (1, sizeof (cairo_nexus_surface_t));
    if (surface == NULL)
        return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));

    /* XXX nexus -> device */
    _cairo_surface_init (&surface->image.base,
                         &_cairo_nexus_surface_backend,
                         NULL, /* device */
                         _nexus_format_to_content (format));

    NEXUS_Surface_GetMemory(nexusSurface->handle, &mem);
    ((char *)mem.buffer)[0] = 0;
    image = pixman_image_create_bits (pixman_format,
                                      nexusSurface->getWidth(nexusSurface),
                                      nexusSurface->getHeight(nexusSurface),
                                      mem.buffer, mem.pitch);
    if (image == NULL) {
        nexusSurface->unlock(nexusSurface);
        free(surface);
        return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));
    }
    _cairo_image_surface_init (&surface->image, image, pixman_format);
    
    surface->nexus = nexus;
    surface->nexusSurface = nexusSurface;
    nexusSurface->addRef (nexusSurface);

    return &surface->image.base;
}
slim_hidden_def(cairo_nexus_surface_create);
