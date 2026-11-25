/* Fexox OS Project, EULA GPLv2 */
/*
 * Small JPEG decoder: baseline + progressive Huffman, 8 bit, grayscale / YCbCr (any common
 * subsampling), restart markers, integer maths only (no SSE), streaming input.
 *
 * The decoder can produce the picture at 1/1, 1/2, 1/4 or 1/8 size straight from the DCT
 * coefficients (scaled IDCT): it picks the smallest reduction whose result is still at least
 * want_w x want_h, so a 6000x4000 photo for a 1920x1080 screen is decoded at 1/2 size, using a
 * fraction of the time and memory.  Pass want_w = want_h = 0 for full size.
 */
#ifndef JPEG_H
#define JPEG_H

#include <stdint.h>
#include <stddef.h>
#include "png.h"                       /* png_image_t: width, height, pixels (RGBA, kmalloc'd) */

#define JPEG_OK                0
#define JPEG_ERR_FORMAT      (-1)
#define JPEG_ERR_UNSUPPORTED (-2)      /* arithmetic coding, 12 bit, CMYK, lossless ... */
#define JPEG_ERR_NOMEM       (-3)
#define JPEG_ERR_IO          (-4)

/* Input callback: copy up to `max` bytes into buf, return the count, 0 / <0 at the end. */
typedef int (*jpeg_read_fn)(void *ctx, uint8_t *buf, uint32_t max);

int jpeg_decode_stream(jpeg_read_fn rd, void *ctx, uint32_t want_w, uint32_t want_h,
                       png_image_t *out);
int jpeg_decode(const uint8_t *data, size_t size, uint32_t want_w, uint32_t want_h,
                png_image_t *out);

/* Reads only the header: size of the picture. */
int jpeg_probe(const uint8_t *data, size_t size, uint32_t *w, uint32_t *h);

#endif
