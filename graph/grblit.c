/****************************************************************************/
/*                                                                          */
/*  The FreeType project -- a free and portable quality TrueType renderer.  */
/*                                                                          */
/*  Copyright (C) 1996-2021 by                                              */
/*  D. Turner, R.Wilhelm, and W. Lemberg                                    */
/*                                                                          */
/*  grblit.c: Support for blitting of bitmaps with various depth.           */
/*                                                                          */
/****************************************************************************/

#include "grblit.h"

#define  GRAY8

  static
  int  compute_clips( grBlitter*  blit,
                      int         x_offset,
                      int         y_offset )
  {
    int  xmin, ymin, xmax, ymax, width, height, target_width;

    /* perform clipping and setup variables */
    width  = blit->source.width;
    height = blit->source.rows;

    switch ( blit->source.mode )
    {
    case gr_pixel_mode_mono:
      width = (width + 7) & -8;
      break;

    case gr_pixel_mode_pal4:
      width = (width + 1) & -2;
      break;

    case gr_pixel_mode_lcd:
    case gr_pixel_mode_lcd2:
      width /= 3;
      break;

    case gr_pixel_mode_lcdv:
    case gr_pixel_mode_lcdv2:
      height /= 3;
      break;

    default:
      ;
    }

    xmin = x_offset;
    ymin = y_offset;
    xmax = xmin + width-1;
    ymax = ymin + height-1;

    /* clip if necessary */
    if ( width == 0 || height == 0                ||
         xmax < 0   || xmin >= blit->target.width ||
         ymax < 0   || ymin >= blit->target.rows  )
      return 1;

    /* set up clipping and cursors */
    blit->yread = 0;
    if ( ymin < 0 )
    {
      blit->yread  -= ymin;
      height       += ymin;
      blit->ywrite  = 0;
    }
    else
      blit->ywrite  = ymin;

    if ( ymax >= blit->target.rows )
      height -= ymax - blit->target.rows + 1;

    blit->xread = 0;
    if ( xmin < 0 )
    {
      blit->xread  -= xmin;
      width        += xmin;
      blit->xwrite  = 0;
    }
    else
      blit->xwrite  = xmin;

    target_width = blit->target.width;

    switch ( blit->target.mode )
    {
    case gr_pixel_mode_mono:
      target_width = (target_width + 7) & -8;
      break;
    case gr_pixel_mode_pal4:
      target_width = (target_width + 1) & -2;
      break;

    default:
      ;
    }

    blit->right_clip = xmax - target_width + 1;
    if ( blit->right_clip > 0 )
      width -= blit->right_clip;
    else
      blit->right_clip = 0;

    blit->width  = width;
    blit->height = height;

    /* set read and write to the top-left corner of the read */
    /* and write areas before clipping.                      */

    blit->read  = blit->source.buffer;
    blit->write = blit->target.buffer;

    blit->read_line  = blit->source.pitch;
    blit->write_line = blit->target.pitch;

    if ( blit->read_line < 0 )
      blit->read -= (blit->source.rows-1) * blit->read_line;

    if ( blit->write_line < 0 )
      blit->write -= (blit->target.rows-1) * blit->write_line;

    /* now go to the start line. Note that we do not move the   */
    /* x position yet, as this is dependent on the pixel format */
    blit->read  += blit->yread * blit->read_line;
    blit->write += blit->ywrite * blit->write_line;

    return 0;
  }


/**************************************************************************/
/*                                                                        */
/* <Function> blit_mono_to_mono                                           */
/*                                                                        */
/**************************************************************************/

  static
  void  blit_mono_to_mono( grBlitter*  blit,
                           grColor     color )
  {
    unsigned int  shift;
    int           left_clip, x, y;

    byte*  read;
    byte*  write;

    (void)color;   /* unused argument */

    left_clip = ( blit->xread > 0 );
    shift     = (unsigned int)( blit->xwrite - blit->xread ) & 7;

    read  = blit->read  + (blit->xread >> 3);
    write = blit->write + (blit->xwrite >> 3);

    if ( shift == 0 )
    {
      y = blit->height;
      do
      {
        byte*  _read  = read;
        byte*  _write = write;

        x = blit->width;

        do
        {
          *_write++ |= *_read++;
          x -= 8;
        } while ( x > 0 );

        read  += blit->read_line;
        write += blit->write_line;
        y--;
      } while ( y > 0 );
    }
    else
    {
      int  first, last, count;


      first = blit->xwrite >> 3;
      last  = (blit->xwrite + blit->width-1) >> 3;

      count = last - first;

      if ( blit->right_clip )
        count++;

      y = blit->height;

      do
      {
        unsigned char*  _read  = read;
        unsigned char*  _write = write;
        unsigned int    old;
        unsigned int    shift2 = (8-shift);

        if ( left_clip )
          old = (unsigned int)(*_read++) << shift2;
        else
          old = 0;

        x = count;
        while ( x > 0 )
        {
          unsigned int  val = *_read++;

          *_write++ |= (unsigned char)( (val >> shift) | old );
          old = val << shift2;
          x--;
        }

        if ( !blit->right_clip )
          *_write |= (unsigned char)old;

        read  += blit->read_line;
        write += blit->write_line;
        y--;

      } while ( y > 0 );
    }
  }


/**************************************************************************/
/*                                                                        */
/* <Function> blit_mono_to_pal8                                           */
/*                                                                        */
/**************************************************************************/

  static
  void  blit_mono_to_pal8( grBlitter*  blit,
                           grColor     color )
  {
    int             x, y;
    unsigned char*  write = blit->write + blit->xwrite;
    unsigned char*  read  = blit->read  + ( blit->xread >> 3 );
    unsigned int    mask  = 0x80 >> ( blit->xread & 7 );

    y = blit->height;
    do
    {
      unsigned char*  _write = write;
      unsigned char*  _read  = read;
      unsigned int    _mask  = mask;
      unsigned int     val   = *_read;

      x = blit->width;
      do
      {
        if ( !_mask )
        {
          val   = *++_read;
          _mask = 0x80;
        }

        if ( val & _mask )
          *_write = (unsigned char)color.value;

        _mask >>= 1;
        _write++;

      } while ( --x > 0 );

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    } while ( y > 0 );
  }


/**************************************************************************/
/*                                                                        */
/* <Function> blit_mono_to_pal4                                           */
/*                                                                        */
/**************************************************************************/

  static
  void  blit_mono_to_pal4( grBlitter*  blit,
                           grColor     color )
  {
    int             x, y;
    unsigned char*  write = blit->write + ( blit->xwrite >> 1 );
    unsigned int    phase = blit->xwrite & 1 ? 0x0F : 0xF0;
    unsigned char*  read  = blit->read  + ( blit->xread >> 3 );
    unsigned int    mask  = 0x80 >> ( blit->xread & 7 );
    unsigned int    col = color.value | ( color.value << 4 );

    y = blit->height;
    do
    {
      unsigned char*  _write = write;
      unsigned int    _phase = phase;
      unsigned char*  _read  = read;
      unsigned int    _mask  = mask;
      unsigned int     val   = *_read;

      x = blit->width;
      do
      {
        if ( !_mask )
        {
          val   = *++_read;
          _mask = 0x80;
        }

        if ( val & _mask )
          *_write = (unsigned char)( (col & _phase) | (*_write & ~_phase) );

        _mask >>= 1;
        _write += _phase & 1;
        _phase  = ~_phase;
        x--;
      } while ( x > 0 );

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    } while ( y > 0 );
  }


/**************************************************************************/
/*                                                                        */
/* <Function> blit_mono_to_rgb16                                          */
/*                                                                        */
/**************************************************************************/

  static
  void  blit_mono_to_rgb16( grBlitter*  blit,
                            grColor     color )
  {
    int              x, y;
    unsigned char*   write = blit->write + blit->xwrite * 2;
    unsigned char*   read  = blit->read  + ( blit->xread >> 3 );
    unsigned int     mask  = 0x80 >> ( blit->xread & 7 );

    y = blit->height;
    do
    {
      unsigned short* _write = (unsigned short*)write;
      unsigned char*  _read  = read;
      unsigned int    _mask  = mask;
      unsigned int     val   = *_read;

      x = blit->width;
      do
      {
        if ( !_mask )
        {
          val   = *++_read;
          _mask = 0x80;
        }

        if ( val & _mask )
          *_write = (unsigned short)color.value;

        _mask >>= 1;
        _write++;
        x--;
      } while ( x > 0 );

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    } while ( y > 0 );
  }


/**************************************************************************/
/*                                                                        */
/* <Function> blit_mono_to_rgb24                                          */
/*                                                                        */
/**************************************************************************/

  static
  void  blit_mono_to_rgb24( grBlitter*  blit,
                            grColor     color )
  {
    int             x, y;
    unsigned char*  write = blit->write + blit->xwrite * 3;
    unsigned char*  read  = blit->read  + ( blit->xread >> 3 );
    unsigned int    mask  = 0x80 >> ( blit->xread & 7 );

    y = blit->height;
    do
    {
      unsigned char*  _write = write;
      unsigned char*  _read  = read;
      unsigned int    _mask  = mask;
      unsigned int     val   = *_read;

      x = blit->width;
      do
      {
        if ( !_mask )
        {
          val   = *++_read;
          _mask = 0x80;
        }

        if ( val & _mask )
        {
          _write[0] = color.chroma[0];
          _write[1] = color.chroma[1];
          _write[2] = color.chroma[2];
        }

        _mask >>= 1;
        _write += 3;
        x--;
      } while ( x > 0 );

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    } while ( y > 0 );
  }


/**************************************************************************/
/*                                                                        */
/* <Function> blit_mono_to_rgb32                                          */
/*                                                                        */
/**************************************************************************/

  static
  void  blit_mono_to_rgb32( grBlitter*  blit,
                            grColor     color )
  {
    int             x, y;
    unsigned char*  write = blit->write + blit->xwrite * 4;
    unsigned char*  read  = blit->read  + ( blit->xread >> 3 );
    unsigned int    mask  = 0x80 >> ( blit->xread & 7 );

    y = blit->height;
    do
    {
      uint32_t*       _write = (uint32_t*)write;
      unsigned char*  _read  = read;
      unsigned int    _mask  = mask;
      unsigned int     val   = *_read;

      x = blit->width;
      do
      {
        if ( !_mask )
        {
          val   = *++_read;
          _mask = 0x80;
        }

        if ( val & _mask )
          *_write = color.value;

        _mask >>= 1;
        _write++;
        x--;

      } while ( x > 0 );

      read  += blit->read_line;
      write += blit->write_line;
      y--;

    } while ( y > 0 );
  }


  static
  const grBlitterFunc  gr_mono_blitters[gr_pixel_mode_max] =
  {
    0,
    blit_mono_to_mono,
    blit_mono_to_pal4,
    blit_mono_to_pal8,
    blit_mono_to_pal8,
    blit_mono_to_rgb16,
    blit_mono_to_rgb16,
    blit_mono_to_rgb24,
    blit_mono_to_rgb32
  };


  /*******************************************************************/
  /*                                                                 */
  /*                    Saturation tables                            */
  /*                                                                 */
  /*******************************************************************/

  typedef struct grSaturation_
  {
    int          count;
    const byte*  table;

  } grSaturation;


  static
  const byte  gr_saturation_5[8] = { 0, 1, 2, 3, 4, 4, 4, 4 };


  static
  const byte  gr_saturation_17[32] =
  {
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
  };


  static
  grSaturation  gr_saturations[ GR_MAX_SATURATIONS ] =
  {
    {  5, gr_saturation_5  },
    { 17, gr_saturation_17 }
  };

  static
  int  gr_num_saturations = 2;

  static
  grSaturation*  gr_last_saturation = gr_saturations;


  static const byte*
  grGetSaturation( int  num_grays )
  {
    /* first of all, scan the current saturations table */
    grSaturation*  sat   = gr_saturations;
    grSaturation*  limit = sat + gr_num_saturations;

    if ( num_grays < 2 )
    {
      grError = gr_err_bad_argument;
      return NULL;
    }

    for ( ; sat < limit; sat++ )
    {
      if ( sat->count == num_grays )
      {
        gr_last_saturation = sat;
        return sat->table;
      }
    }

    /* not found, simply create a new entry if there is room */
    if (gr_num_saturations < GR_MAX_SATURATIONS)
    {
      int    i;
      byte*  table;

      table = grAlloc( (size_t)( 3 * num_grays - 1 ) * sizeof ( byte ) );
      if (!table)
        return NULL;

      sat->count = num_grays;
      sat->table = table;

      for ( i = 0; i < num_grays; i++, table++ )
        *table = (byte)i;

      for ( i = 2*num_grays-1; i > 0; i--, table++ )
        *table = (byte)(num_grays-1);

      gr_num_saturations++;
      gr_last_saturation = sat;
      return table;
    }
    grError = gr_err_saturation_overflow;
    return NULL;
  }



  /*******************************************************************/
  /*                                                                 */
  /*                    conversion tables                            */
  /*                                                                 */
  /*******************************************************************/

  typedef struct grConversion_
  {
    int          target_grays;
    int          source_grays;
    const byte*  table;

  } grConversion;



  static
  const byte  gr_gray5_to_gray17[5] = { 0, 4, 8, 12, 16 };


  static
  const byte  gr_gray5_to_gray128[5] = { 0, 32, 64, 96, 127 };


  static
  const unsigned char  gr_gray17_to_gray128[17] =
  {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 127
  };

  static
  grConversion  gr_conversions[ GR_MAX_CONVERSIONS ] =
  {
    {  17,  5, gr_gray5_to_gray17   },
    { 128,  5, gr_gray5_to_gray128  },
    { 128, 17, gr_gray17_to_gray128 }
  };

  static
  int  gr_num_conversions = 3;

  static
  grConversion*  gr_last_conversion = gr_conversions;


  static const byte*
  grGetConversion( int  target_grays,
                   int  source_grays )
  {
    grConversion*  conv  = gr_conversions;
    grConversion*  limit = conv + gr_num_conversions;

    if ( target_grays < 2 || source_grays < 2 )
    {
      grError = gr_err_bad_argument;
      return NULL;
    }

    /* otherwise, scan table */
    for ( ; conv < limit; conv++ )
    {
      if ( conv->target_grays == target_grays &&
           conv->source_grays == source_grays )
      {
        gr_last_conversion = conv;
        return conv->table;
      }
    }

    /* not found, add a new conversion to the table */
    if (gr_num_conversions < GR_MAX_CONVERSIONS)
    {
      byte*  table;
      int    n;

      table = grAlloc( (size_t)source_grays * sizeof ( byte ) );
      if (!table)
        return NULL;

      conv->target_grays = target_grays;
      conv->source_grays = source_grays;
      conv->table        = table;

      for ( n = 0; n < source_grays; n++ )
        table[n] = (byte)(n*(target_grays-1) / (source_grays-1));

      gr_num_conversions++;
      gr_last_conversion = conv;
      return table;
    }
    grError = gr_err_conversion_overflow;
    return NULL;
  }




/**************************************************************************/
/*                                                                        */
/* <Function> blit_gray_to_gray                                           */
/*                                                                        */
/**************************************************************************/

  static
  void  blit_gray_to_gray( grBlitter*   blit,
                           const byte*  saturation,
                           const byte*  conversion )
  {
    int             y;
    unsigned char*  read;
    unsigned char*  write;
#ifdef GR_CONFIG_GRAY_SKIP_WHITE
    int  max = blit->source.grays - 1;
    int  max2 = blit-target.grays - 1;
#endif

    read  = blit->read  + blit->xread;
    write = blit->write + blit->xwrite;

    y = blit->height;
    do
    {
      unsigned char*  _read  = read;
      unsigned char*  _write = write;
      int             x      = blit->width;

      while (x > 0)
      {
#ifdef GR_CONFIG_GRAY_SKIP_WHITE
        int  val = *_read;

        if (val)
        {
          if (val == max)
            *_write = (byte)max2;
          else
            *_write = saturation[ (int)*_write + conversion[ *_read ] ];
        }
#else
        *_write = saturation[ (int)*_write + conversion[ *_read ] ];
#endif
        _write++;
        _read++;
        x--;
      }

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }


/**************************************************************************/
/*                                                                        */
/* <Function> blit_gray_to_gray_simple                                    */
/*                                                                        */
/**************************************************************************/

  static
  void  blit_gray_to_gray_simple( grBlitter*   blit,
                                  const byte*  saturation )
  {
    int             y;
    unsigned char*  read;
    unsigned char*  write;
#ifdef GR_CONFIG_GRAY_SKIP_WHITE
    int  max = blit->source.grays - 1;
#endif

    read  = blit->read  + blit->xread;
    write = blit->write + blit->xwrite;

    y = blit->height;
    do
    {
      unsigned char*  _read  = read;
      unsigned char*  _write = write;
      int             x      = blit->width;

      while (x > 0)
      {
#ifdef GR_CONFIG_GRAY_SKIP_WHITE
        int  val = *_read;

        if (val)
        {
          if (val == max)
            *_write = val;
          else
            *_write = saturation[ (int)*_write + *_read ];
        }
#else
        *_write = saturation[ (int)*_write + *_read ];
#endif
        _write++;
        _read++;
        x--;
      }

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }



#define compose_pixel_full( a, b, n0, n1, n2, max )    \
  do                                                   \
  {                                                    \
    int  d, half = max >> 1;                           \
                                                       \
                                                       \
    d = (int)b.chroma[0] - a.chroma[0];                \
    a.chroma[0] += (unsigned char)((n0*d + half)/max); \
                                                       \
    d = (int)b.chroma[1] - a.chroma[1];                \
    a.chroma[1] += (unsigned char)((n1*d + half)/max); \
                                                       \
    d = (int)b.chroma[2] - a.chroma[2];                \
    a.chroma[2] += (unsigned char)((n2*d + half)/max); \
  } while ( 0 )

#define compose_pixel( a, b, n, max )  \
    compose_pixel_full( a, b, n, n, n, max )


#define extract555( pixel, color )                           \
   color.chroma[0] = (unsigned char)((pixel >> 10) & 0x1F);  \
   color.chroma[1] = (unsigned char)((pixel >>  5) & 0x1F);  \
   color.chroma[2] = (unsigned char)((pixel      ) & 0x1F)


#define extract565( pixel, color )                           \
   color.chroma[0] = (unsigned char)((pixel >> 11) & 0x1F);  \
   color.chroma[1] = (unsigned char)((pixel >>  5) & 0x3F);  \
   color.chroma[2] = (unsigned char)((pixel      ) & 0x1F)


#define inject555( color )                          \
   (unsigned short)( ( color.chroma[0] << 10 ) |    \
                     ( color.chroma[1] <<  5 ) |    \
                       color.chroma[2]           )


#define inject565( color )                          \
   (unsigned short)( ( color.chroma[0] << 11 ) |    \
                     ( color.chroma[1] <<  5 ) |    \
                       color.chroma[2]           )


/**************************************************************************/
/*                                                                        */
/* <Function> blit_gray_to_555                                            */
/*                                                                        */
/**************************************************************************/

#ifdef GRAY8
  static
  void  blit_gray8_to_555( grBlitter*  blit,
                           grColor     color )
  {
    int             y;
    int             sr = color.value & 0x7C00;
    int             sg = color.value & 0x03E0;
    int             sb = color.value & 0x001F;
    unsigned char*  read;
    unsigned char*  write;

    read   = blit->read  + blit->xread;
    write  = blit->write + 2*blit->xwrite;

    y = blit->height;
    do
    {
      unsigned char*   _read  = read;
      unsigned short*  _write = (unsigned short*)write;
      int              x      = blit->width;

      while (x > 0)
      {
        unsigned char  val = *_read;

        if (val)
        {
          if (val >= 254 )
            *_write = (unsigned short)( sr | sg | sb );
          else if ( val >= 2 )
          {
            /* compose gray value */
            int   pix = (int)*_write;
            int   dr  = pix & 0x7C00;
            int   dg  = pix & 0x03E0;
            int   db  = pix & 0x001F;

            dr += ((sr-dr)*val) >> 8;
            dr &= 0x7C00;

            dg += ((sg-dg)*val) >> 8;
            dg &= 0x03E0;

            db += ((sb-db)*val) >> 8;
            db &= 0x001F;

            *_write = (unsigned short)( dr | dg | db );
          }
        }
        _write ++;
        _read  ++;
        x--;
      }

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);

  }
#endif /* GRAY8 */

  static
  void  blit_gray_to_555( grBlitter*  blit,
                          grColor     color,
                          int         max )
  {
    int             y;
    unsigned char*  read;
    unsigned char*  write;

    read   = blit->read  + blit->xread;
    write  = blit->write + 2*blit->xwrite;

    /* scale down R:G:B triplet */
    color.chroma[0] >>= 3;
    color.chroma[1] >>= 3;
    color.chroma[2] >>= 3;

    y = blit->height;
    do
    {
      unsigned char*   _read  = read;
      unsigned short*  _write = (unsigned short*)write;
      int              x      = blit->width;

      while (x > 0)
      {
        int  val = *_read;

        if (val)
        {
          if (val == max)
            *_write = inject555( color );
          else
          {
            /* compose gray value */
            unsigned short  pix16 = *_write;
            grColor         pix;

            extract555( pix16, pix );

            compose_pixel( pix, color, val, max );
            *_write = inject555( pix );
          }
        }
        _write ++;
        _read  ++;
        x--;
      }

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }


/**************************************************************************/
/*                                                                        */
/* <Function> blit_gray_to_565                                            */
/*                                                                        */
/**************************************************************************/

#ifdef GRAY8
  static
  void  blit_gray8_to_565( grBlitter*  blit,
                           grColor     color )
  {
    int             y;
    int             sr = color.value & 0xF800;
    int             sg = color.value & 0x07E0;
    int             sb = color.value & 0x001F;
    unsigned char*  read;
    unsigned char*  write;

    read   = blit->read  + blit->xread;
    write  = blit->write + 2*blit->xwrite;

    y = blit->height;
    do
    {
      unsigned char*   _read  = read;
      unsigned short*  _write = (unsigned short*)write;
      int              x      = blit->width;

      while (x > 0)
      {
        unsigned char  val = *_read;

        if (val)
        {
          if (val >= 254 )
            *_write = (unsigned short)( sr | sg | sb );
          else if ( val >= 2 )
          {
            /* compose gray value */
            int   pix = (int)*_write;
            int   dr  = pix & 0xF800;
            int   dg  = pix & 0x07E0;
            int   db  = pix & 0x001F;

            dr += ((sr-dr)*val) >> 8;
            dr &= 0xF800;

            dg += ((sg-dg)*val) >> 8;
            dg &= 0x07E0;

            db += ((sb-db)*val) >> 8;
            db &= 0x001F;

            *_write = (unsigned short)( dr | dg | db );
          }
        }
        _write ++;
        _read  ++;
        x--;
      }

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }
#endif

  static
  void  blit_gray_to_565( grBlitter*  blit,
                          grColor     color,
                          int         max )
  {
    int             y;
    unsigned char*  read;
    unsigned char*  write;

    read   = blit->read  + blit->xread;
    write  = blit->write + 2*blit->xwrite;

    /* scale down R:G:B triplet */
    color.chroma[0] >>= 3;
    color.chroma[1] >>= 2;
    color.chroma[2] >>= 3;

    y = blit->height;
    do
    {
      unsigned char*   _read  = read;
      unsigned short*  _write = (unsigned short*)write;
      int              x      = blit->width;

      while (x > 0)
      {
        int  val = *_read;

        if (val)
        {
          if (val == max)
            *_write = inject565( color );
          else
          {
            /* compose gray value */
            unsigned short  pix16 = *_write;
            grColor         pix;

            extract565( pix16, pix );

            compose_pixel( pix, color, val, max );
            *_write = inject565( pix );
          }
        }
        _write ++;
        _read  ++;
        x--;
      }

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }


/**************************************************************************/
/*                                                                        */
/* <Function> blit_gray_to_24                                             */
/*                                                                        */
/**************************************************************************/

#ifdef GRAY8
  static void
  blit_gray8_to_24( grBlitter*  blit,
                    grColor     color )
  {
    int             y;
    int             sr = color.chroma[0];
    int             sg = color.chroma[1];
    int             sb = color.chroma[2];
    unsigned char*  read;
    unsigned char*  write;

    read   = blit->read  + blit->xread;
    write  = blit->write + 3*blit->xwrite;

    y = blit->height;
    do
    {
      unsigned char*  _read  = read;
      unsigned char*  _write = write;
      int             x      = blit->width;

      while (x > 0)
      {
        unsigned char  val = *_read;

        if (val)
        {
          if (val >= 254)
          {
            _write[0] = (unsigned char)sr;
            _write[1] = (unsigned char)sg;
            _write[2] = (unsigned char)sb;
          }
          else if ( val >= 2 )
          {
            int  dr = _write[0];
            int  dg = _write[1];
            int  db = _write[2];

            dr += ((sr-dr)*val) >> 8;
            dg += ((sg-dg)*val) >> 8;
            db += ((sb-db)*val) >> 8;

            _write[0] = (unsigned char)dr;
            _write[1] = (unsigned char)dg;
            _write[2] = (unsigned char)db;
          }
        }
        _write += 3;
        _read  ++;
        x--;
      }

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }
#endif /* GRAY8 */


  static
  void  blit_gray_to_24( grBlitter*  blit,
                         grColor     color,
                         int         max )
  {
    int             y;
    unsigned char*  read;
    unsigned char*  write;

    read   = blit->read  + blit->xread;
    write  = blit->write + 3*blit->xwrite;

    y = blit->height;
    do
    {
      unsigned char*  _read  = read;
      unsigned char*  _write = write;
      int             x      = blit->width;

      while (x > 0)
      {
        int  val = *_read;

        if (val)
        {
          if (val == max)
          {
            _write[0] = color.chroma[0];
            _write[1] = color.chroma[1];
            _write[2] = color.chroma[2];
          }
          else
          {
            /* compose gray value */
            grColor pix;

            pix.chroma[0] = _write[0];
            pix.chroma[1] = _write[1];
            pix.chroma[2] = _write[2];

            compose_pixel( pix, color, val, max );

            _write[0] = pix.chroma[0];
            _write[1] = pix.chroma[1];
            _write[2] = pix.chroma[2];
          }
        }
        _write += 3;
        _read  ++;
        x--;
      }

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }


/**************************************************************************/
/*                                                                        */
/* <Function> blit_gray_to_32                                             */
/*                                                                        */
/**************************************************************************/

  static
  void  blit_gray_to_32( grBlitter*  blit,
                         grColor     color,
                         int         max )
  {
    int             y;
    unsigned char*  read;
    unsigned char*  write;

    read   = blit->read  + blit->xread;
    write  = blit->write + 4*blit->xwrite;

    y = blit->height;
    do
    {
      unsigned char*  _read  = read;
      unsigned char*  _write = write;
      int             x      = blit->width;

      while (x > 0)
      {
        int  val = *_read;

        if (val)
        {
          if (val == max)
          {
            _write[0] = color.chroma[0];
            _write[1] = color.chroma[1];
            _write[2] = color.chroma[2];
            _write[3] = color.chroma[3];
          }
          else
          {
            /* compose gray value */
            grColor pix;

            pix.chroma[0] = _write[0];
            pix.chroma[1] = _write[1];
            pix.chroma[2] = _write[2];

            compose_pixel( pix, color, val, max );

            _write[0] = pix.chroma[0];
            _write[1] = pix.chroma[1];
            _write[2] = pix.chroma[2];
          }
        }
        _write += 4;
        _read  ++;
        x--;
      }

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }


  static
  void  blit_gray8_to_32( grBlitter*  blit,
                          grColor     color )
  {
    blit_gray_to_32( blit, color, 255 );
  }


/**************************************************************************/
/*                                                                        */
/* <Function> blit_lcd_to_24                                              */
/*                                                                        */
/**************************************************************************/

#ifdef GRAY8
  static void
  blit_lcd8_to_24( grBlitter*  blit,
                   grColor     color )
  {
    int             y;
    int             sr = color.chroma[0];
    int             sg = color.chroma[1];
    int             sb = color.chroma[2];
    unsigned char*  read;
    unsigned char*  write;

    read   = blit->read  + 3*blit->xread;
    write  = blit->write + 3*blit->xwrite;

    y = blit->height;
    do
    {
      unsigned char*  _read  = read;
      unsigned char*  _write = write;
      int             x      = blit->width;

      while (x > 0)
      {
        int  val0 = _read[0];
        int  val1 = _read[1];
        int  val2 = _read[2];

        if ( val0 | val1 | val2 )
        {
          if ( val0 == val1 &&
               val0 == val2 &&
               val0 == 255  )
          {
            _write[0] = (unsigned char)sr;
            _write[1] = (unsigned char)sg;
            _write[2] = (unsigned char)sb;
          }
          else
          {
            /* compose gray value */
            int   dr, dg, db;

            dr  = _write[0];
            dr += (sr-dr)*val0 >> 8;

            dg  = _write[1];
            dg += (sg-dg)*val1 >> 8;

            db  = _write[2];
            db += (sb-db)*val2 >> 8;

            _write[0] = (unsigned char)dr;
            _write[1] = (unsigned char)dg;
            _write[2] = (unsigned char)db;
          }
        }
        _write += 3;
        _read  += 3;
        x--;
      }

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }
#endif /* GRAY8 */

  static void
  blit_lcd_to_24( grBlitter*  blit,
                  grColor     color,
                  int         max )
  {
    int             y;
    unsigned char*  read;
    unsigned char*  write;

    read   = blit->read  + 3*blit->xread;
    write  = blit->write + 3*blit->xwrite;

    y = blit->height;
    do
    {
      unsigned char*  _read  = read;
      unsigned char*  _write = write;
      int             x      = blit->width;

      while (x > 0)
      {
        int  val0 = _read[0];
        int  val1 = _read[1];
        int  val2 = _read[2];

        if ( val0 | val1 | val2 )
        {
          if ( val0 == val1 &&
               val0 == val2 &&
               val0 == max  )
          {
            _write[0] = color.chroma[0];
            _write[1] = color.chroma[1];
            _write[2] = color.chroma[2];
          }
          else
          {
            /* compose gray value */
            grColor pix;

            pix.chroma[0] = _write[0];
            pix.chroma[1] = _write[1];
            pix.chroma[2] = _write[2];

            compose_pixel_full( pix, color, val0, val1, val2, max );

            _write[0] = pix.chroma[0];
            _write[1] = pix.chroma[1];
            _write[2] = pix.chroma[2];
          }
        }
        _write += 3;
        _read  += 3;
        x--;
      }

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }


#ifdef GRAY8
  static void
  blit_lcd28_to_24( grBlitter*  blit,
                    grColor     color )
  {
    int             y;
    int             sr = color.chroma[0];
    int             sg = color.chroma[1];
    int             sb = color.chroma[2];
    unsigned char*  read;
    unsigned char*  write;

    read   = blit->read  + 3*blit->xread;
    write  = blit->write + 3*blit->xwrite;

    y = blit->height;
    do
    {
      unsigned char*  _read  = read;
      unsigned char*  _write = write;
      int             x      = blit->width;

      while (x > 0)
      {
        int  val0 = _read[2];
        int  val1 = _read[1];
        int  val2 = _read[0];

        if ( val0 | val1 | val2 )
        {
          if ( val0 == val1 &&
               val0 == val2 &&
               val0 == 255  )
          {
            _write[0] = (unsigned char)sr;
            _write[1] = (unsigned char)sg;
            _write[2] = (unsigned char)sb;
          }
          else
          {
            /* compose gray value */
            int   dr, dg, db;

            dr  = _write[0];
            dr += (sr-dr)*val0 >> 8;

            dg  = _write[1];
            dg += (sg-dg)*val1 >> 8;

            db  = _write[2];
            db += (sb-db)*val2 >> 8;

            _write[0] = (unsigned char)dr;
            _write[1] = (unsigned char)dg;
            _write[2] = (unsigned char)db;
          }
        }
        _write += 3;
        _read  += 3;
        x--;
      }

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }
#endif /* GRAY8 */

  static void
  blit_lcd2_to_24( grBlitter*  blit,
                   grColor     color,
                   int         max )
  {
    int             y;
    unsigned char*  read;
    unsigned char*  write;

    read   = blit->read  + 3*blit->xread;
    write  = blit->write + 3*blit->xwrite;

    y = blit->height;
    do
    {
      unsigned char*  _read  = read;
      unsigned char*  _write = write;
      int             x      = blit->width;

      while (x > 0)
      {
        int  val0 = _read[2];
        int  val1 = _read[1];
        int  val2 = _read[0];

        if ( val0 | val1 | val2 )
        {
          if ( val0 == val1 &&
               val0 == val2 &&
               val0 == max  )
          {
            _write[0] = color.chroma[0];
            _write[1] = color.chroma[1];
            _write[2] = color.chroma[2];
          }
          else
          {
            /* compose gray value */
            grColor pix;

            pix.chroma[0] = _write[0];
            pix.chroma[1] = _write[1];
            pix.chroma[2] = _write[2];

            compose_pixel_full( pix, color, val0, val1, val2, max );

            _write[0] = pix.chroma[0];
            _write[1] = pix.chroma[1];
            _write[2] = pix.chroma[2];
          }
        }
        _write += 3;
        _read  += 3;
        x--;
      }

      read  += blit->read_line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }


/**************************************************************************/
/*                                                                        */
/* <Function> blit_lcdv_to_24                                             */
/*                                                                        */
/**************************************************************************/

  static void
  blit_lcdv_to_24( grBlitter*  blit,
                   grColor     color,
                   int         max )
  {
    int             y;
    unsigned char*  read;
    unsigned char*  write;
    long            line;

    read   = blit->read  + blit->xread;
    write  = blit->write + 3*blit->xwrite;
    line   = blit->read_line;

    y = blit->height;
    do
    {
      unsigned char*  _read  = read;
      unsigned char*  _write = write;
      int             x      = blit->width;

      while (x > 0)
      {
        int  val0 = _read[0*line];
        int  val1 = _read[1*line];
        int  val2 = _read[2*line];

        if ( val0 | val1 | val2 )
        {
          if ( val0 == val1 &&
               val0 == val2 &&
               val0 == max  )
          {
            _write[0] = color.chroma[0];
            _write[1] = color.chroma[1];
            _write[2] = color.chroma[2];
          }
          else
          {
            /* compose gray value */
            grColor pix;

            pix.chroma[0] = _write[0];
            pix.chroma[1] = _write[1];
            pix.chroma[2] = _write[2];

            compose_pixel_full( pix, color, val0, val1, val2, max );

            _write[0] = pix.chroma[0];
            _write[1] = pix.chroma[1];
            _write[2] = pix.chroma[2];
          }
        }
        _write += 3;
        _read  += 1;
        x--;
      }

      read  += 3*line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }


  static void
  blit_lcdv2_to_24( grBlitter*  blit,
                    grColor     color,
                    int         max )
  {
    int             y;
    unsigned char*  read;
    unsigned char*  write;
    long            line;

    read   = blit->read  + blit->xread;
    write  = blit->write + 3*blit->xwrite;
    line   = blit->read_line;

    y = blit->height;
    do
    {
      unsigned char*  _read  = read;
      unsigned char*  _write = write;
      int             x      = blit->width;

      while (x > 0)
      {
        int  val0 = _read[2*line];
        int  val1 = _read[1*line];
        int  val2 = _read[0*line];

        if ( val0 | val1 | val2 )
        {
          if ( val0 == val1 &&
               val0 == val2 &&
               val0 == max  )
          {
            _write[0] = color.chroma[0];
            _write[1] = color.chroma[1];
            _write[2] = color.chroma[2];
          }
          else
          {
            /* compose gray value */
            grColor pix;

            pix.chroma[0] = _write[0];
            pix.chroma[1] = _write[1];
            pix.chroma[2] = _write[2];

            compose_pixel_full( pix, color, val0, val1, val2, max );

            _write[0] = pix.chroma[0];
            _write[1] = pix.chroma[1];
            _write[2] = pix.chroma[2];
          }
        }
        _write += 3;
        _read  += 1;
        x--;
      }

      read  += 3*line;
      write += blit->write_line;
      y--;
    }
    while (y > 0);
  }


  typedef  void (*grColorGlyphBlitter)( grBlitter*  blit,
                                        grColor     color,
                                        int         max_gray );

  static
  const grColorGlyphBlitter  gr_color_blitters[gr_pixel_mode_max] =
  {
    0,
    0,
    0,
    0,
    0,
    blit_gray_to_555,
    blit_gray_to_565,
    blit_gray_to_24,
    blit_gray_to_32
  };

#ifdef GRAY8
  typedef  void (*grGray8GlyphBlitter)( grBlitter*  blit,
                                        grColor     color );

  static
  const grGray8GlyphBlitter  gr_gray8_blitters[gr_pixel_mode_max] =
  {
    0,
    0,
    0,
    0,
    0,
    blit_gray8_to_555,
    blit_gray8_to_565,
    blit_gray8_to_24,
    blit_gray8_to_32
  };
#endif


  int
  grBlitGlyphToBitmap( grBitmap*  target,
                       grBitmap*  glyph,
                       grPos      x,
                       grPos      y,
                       grColor    color )
  {
    grBlitter    blit;
    grPixelMode  mode;


    /* check arguments */
    if ( !target || !glyph )
    {
      grError = gr_err_bad_argument;
      return -1;
    }

    if ( !glyph->rows || !glyph->width )
    {
      /* nothing to do */
      return 0;
    }

    /* set up blitter and compute clipping.  Return immediately if needed */
    blit.source = *glyph;
    blit.target = *target;
    mode        = target->mode;

    if ( compute_clips( &blit, x, y ) )
      return 0;

    switch ( glyph->mode )
    {
    case gr_pixel_mode_mono:     /* handle monochrome bitmap blitting */
      if ( mode <= gr_pixel_mode_none || mode >= gr_pixel_mode_max )
      {
        grError = gr_err_bad_source_depth;
        return -1;
      }

      gr_mono_blitters[mode]( &blit, color );
      break;

    case gr_pixel_mode_gray:
      if ( glyph->grays > 1 )
      {
        int          target_grays = target->grays;
        int          source_grays = glyph->grays;
        const byte*  saturation;


        if ( mode == gr_pixel_mode_gray && target_grays > 1 )
        {
          /* rendering into a gray target - use special composition */
          /* routines..                                             */
          if ( gr_last_saturation->count == target_grays )
            saturation = gr_last_saturation->table;
          else
          {
            saturation = grGetSaturation( target_grays );
            if ( !saturation )
              return -3;
          }

          if ( target_grays == source_grays )
            blit_gray_to_gray_simple( &blit, saturation );
          else
          {
            const byte*  conversion;


            if ( gr_last_conversion->target_grays == target_grays &&
                 gr_last_conversion->source_grays == source_grays )
              conversion = gr_last_conversion->table;
            else
            {
              conversion = grGetConversion( target_grays, source_grays );
              if ( !conversion )
                return -3;
            }

            blit_gray_to_gray( &blit, saturation, conversion );
          }
        }
        else
        {
          /* rendering into a color target */
          if ( mode <= gr_pixel_mode_gray ||
               mode >= gr_pixel_mode_max  )
          {
            grError = gr_err_bad_target_depth;
            return -1;
          }

#ifdef GRAY8
          if ( source_grays == 256 )
            gr_gray8_blitters[mode]( &blit, color );
          else
#endif /* GRAY8 */
          gr_color_blitters[mode]( &blit, color, source_grays - 1 );
        }
      }
      break;

    case gr_pixel_mode_lcd:
      if ( mode == gr_pixel_mode_rgb24 )
      {
#ifdef GRAY8
        if ( glyph->grays == 256 )
          blit_lcd8_to_24( &blit, color );
        else
#endif
        if ( glyph->grays > 1 )
          blit_lcd_to_24( &blit, color, glyph->grays-1 );
      }
      break;


    case gr_pixel_mode_lcdv:
      if ( glyph->grays > 1 && mode == gr_pixel_mode_rgb24 )
      {
        blit_lcdv_to_24( &blit, color, glyph->grays-1 );
        break;
      }
      /* fall through */

    case gr_pixel_mode_lcd2:
      if ( mode == gr_pixel_mode_rgb24 )
      {
#ifdef GRAY8
        if ( glyph->grays == 256 )
          blit_lcd28_to_24( &blit, color );
        else
#endif
        if ( glyph->grays > 1 )
          blit_lcd2_to_24( &blit, color, glyph->grays-1 );
      }
      break;

    case gr_pixel_mode_lcdv2:
      if ( mode == gr_pixel_mode_rgb24 )
      {
        if ( glyph->grays > 1 )
          blit_lcdv2_to_24( &blit, color, glyph->grays-1 );
      }
      break;

    default:
      /* we don't support the blitting of bitmaps of the following  */
      /* types : pal4, pal8, rgb555, rgb565, rgb24, rgb32           */
      /*                                                            */
      grError = gr_err_bad_source_depth;
      return -2;
    }

    return 0;
  }


/* End */
