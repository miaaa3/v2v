// Single translation unit that pulls in the implementation of stb_image.
// stb_image.h itself stays header-only; including it normally elsewhere just
// gets declarations.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>
