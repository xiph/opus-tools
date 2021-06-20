#include <ogg/ogg.h>

typedef enum {
  PIC_FORMAT_JPEG,
  PIC_FORMAT_PNG,
  PIC_FORMAT_GIF
} picture_format;

int is_jpeg(const unsigned char *buf, size_t length);
int is_png(const unsigned char *buf, size_t length);
int is_gif(const unsigned char *buf, size_t length);

void extract_png_params(const unsigned char *data, size_t data_length,
                        ogg_uint32_t *width, ogg_uint32_t *height,
                        ogg_uint32_t *depth, ogg_uint32_t *colors,
                        int *has_palette);
void extract_gif_params(const unsigned char *data, size_t data_length,
                        ogg_uint32_t *width, ogg_uint32_t *height,
                        ogg_uint32_t *depth, ogg_uint32_t *colors,
                        int *has_palette);
void extract_jpeg_params(const unsigned char *data, size_t data_length,
                         ogg_uint32_t *width, ogg_uint32_t *height,
                         ogg_uint32_t *depth, ogg_uint32_t *colors,
                         int *has_palette);
