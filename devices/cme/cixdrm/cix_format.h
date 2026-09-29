/*
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef CIX_FORMAT_H
#define CIX_FORMAT_H

struct util_color_component {
	unsigned int length;
	unsigned int offset;
};

struct util_rgb_info {
	struct util_color_component red;
	struct util_color_component green;
	struct util_color_component blue;
	struct util_color_component alpha;
};

//enum util_yuv_order {
const unsigned int YUV_YCbCr = 1;
const unsigned int YUV_YCrCb = 2;
const unsigned int YUV_YC = 4;
const unsigned int YUV_CY = 8;
//};

struct util_yuv_info {
	//enum util_yuv_order order;
	unsigned int order;
	unsigned int xsub;
	unsigned int ysub;
	unsigned int chroma_stride;
};

struct util_format_info {
	uint32_t format;
	const char *name;
	uint32_t bpp;
	const struct util_rgb_info rgb;
	const struct util_yuv_info yuv;
};

uint32_t util_format_fourcc(const char *name);
const struct util_format_info *util_format_info_find(uint32_t format);
int util_format_nplanes(uint32_t format);
uint32_t util_format_bpp(uint32_t format);
uint32_t util_pixel_format_plane_pitch(int width, int format, int plane);
uint32_t util_pixel_format_plane_height(int height, int format, int plane);
uint8_t util_pixel_format_plane_cpp(uint32_t format, int plane);
uint8_t util_pixel_format_horz_chroma_subsampling(uint32_t format);
uint8_t util_pixel_format_vert_chroma_subsampling(uint32_t format);

#endif /* CIX_FORMAT_H */
