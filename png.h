/* Fexox OS Project, EULA GPLv2 */
 


#ifndef PNG_H
#define PNG_H

#include <stdint.h>
#include <stddef.h>

#define PNG_OK              0
#define PNG_ERR_MAGIC      (-1)
#define PNG_ERR_FORMAT     (-2)
#define PNG_ERR_UNSUPPORTED (-3)
#define PNG_ERR_NOMEM      (-4)
#define PNG_ERR_INFLATE    (-5)

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *pixels;
} png_image_t;

int png_decode(const uint8_t *data, size_t size, png_image_t *out);

#endif
