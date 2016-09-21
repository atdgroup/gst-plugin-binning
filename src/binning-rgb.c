/*
 * Binning Filter GStreamer Plugin
 * Copyright (C) 2015-2016 Gray Cancer Institute
 *
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-binningfilter
 *
 * This element makes some binning on the image, e.g. add pixels together
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc ! videoconvert ! binresize left=50 ! videoconvert ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include "gstbinningfilter.h"

GST_DEBUG_CATEGORY_STATIC (gst_binningfilter_RGB_debug);
#define GST_CAT_DEFAULT gst_binningfilter_RGB_debug

void
gst_bin_image_rgb(Gstbinningfilter *filter, GstBuffer *buf)
{
	unsigned int count=0;
	unsigned int x, y, i, j, val;
	bgr_pixel *ptr=NULL;
	GstMapInfo minfo;

	double *forward_gamma = filter->forward_gamma;
	unsigned int *inverse_gamma = filter->inverse_gamma;

	unsigned int out_limit = OUT_RANGE - 1;
	unsigned int in_limit = IN_RANGE - 1;

	// ***********************************
	// binning the pixels from 24-bit BGR data
	// to do this in-place, always gather pixels from below and right

	// Access the buffer - READ AND WRITE
	gst_buffer_map (buf, &minfo, GST_MAP_READWRITE);

    gint black_r, black_g, black_b;   // RGB black levels that will be subtracted from each pixel
    gint contrast_r, contrast_g, contrast_b;   // RGB contrast values that will be applied to the summed/binned data

	if(filter->format_is_RGB){  // default is BGR, so swap black and contrast b for r
		black_r = filter->black_b; black_g = filter->black_g; black_b = filter->black_r;
		contrast_r = filter->contrast_b; contrast_g = filter->contrast_g; contrast_b = filter->contrast_r;
	}
	else{
		black_r = filter->black_r; black_g = filter->black_g; black_b = filter->black_b;
		contrast_r = filter->contrast_r; contrast_g = filter->contrast_g; contrast_b = filter->contrast_b;
	}

	gint start_y = 0;
	gint stop_y  = filter->height;
	gint start_x = 0;
	gint stop_x  = filter->width;

	guint8 *img_ptr = minfo.data;
	gint pitch = filter->stride / 3;  // want the number of pixels to next line

	gfloat gain_r = contrast_r / 100.0f;    // convert contrast values into real gain factors, contrast=100 => gain=1 => normal summed binning
	gfloat gain_g = contrast_g / 100.0f;
	gfloat gain_b = contrast_b / 100.0f;

	// Check for the special contrast value (-1) to do averaging rather than binning, gain here is dependent in the bin size.
	if (contrast_r < 0)
		gain_r = 1.0f / (filter->binsize*filter->binsize);
	if (contrast_g < 0)
		gain_g = 1.0f / (filter->binsize*filter->binsize);
	if (contrast_b < 0)
		gain_b = 1.0f / (filter->binsize*filter->binsize);

//	GST_DEBUG_OBJECT (filter, "Binsize: %d Gains: %.3f %.3f %.3f, Blacks: %d %d %d", filter->binsize, gain_r, gain_g, gain_b, black_r, black_g, black_b);

	if (filter->binsize == 1){  // no binning here but may want to contrast stretch and apply black levels

		if(gain_r==1.0f && gain_g==1.0f && gain_b==1.0f &&
				black_r==0 && black_g==0 && black_b==0){     // Just check that we have to do anything at all, if not return.
//			GST_DEBUG_OBJECT (filter, "Nothing to do!");
			gst_buffer_unmap (buf, &minfo);
			return;
		}

//		GST_DEBUG_OBJECT (filter, "Apply black or gain.");
		for(y=start_y; y<stop_y; y++){
			ptr = (bgr_pixel *)img_ptr + pitch * y + start_x; // ptr to start of line
			for(x=start_x; x<stop_x; x++){

				// Use 'val' to limit the result without over or under flowing
				val = forward_gamma[CLAMP(ptr->b - black_b, 0, in_limit)];
				ptr->b = inverse_gamma[(unsigned int)CLAMP(val*gain_b, 0, out_limit)];
				val = forward_gamma[CLAMP(ptr->g - black_g, 0, in_limit)];
				ptr->g = inverse_gamma[(unsigned int)CLAMP(val*gain_g, 0, out_limit)];
				val = forward_gamma[CLAMP(ptr->r - black_r, 0, in_limit)];
				ptr->r = inverse_gamma[(unsigned int)CLAMP(val*gain_r, 0, out_limit)];

				count++; // pixel count
				ptr++;  // next pixel, 3 bytes on
			}
		}
	}
	else if (filter->binsize == 2){  // fast implementation for 2x2
		stop_y  = filter->height-1;
		stop_x  = filter->width-1;
		for(y=start_y; y<stop_y; y++){
			ptr = (bgr_pixel *)img_ptr + pitch * y + start_x; // ptr to start of line
			for(x=start_x; x<stop_x; x++){

				// Use 'val' to limit the result without over or under flowing
				val =  forward_gamma[CLAMP(ptr->b - black_b, 0, in_limit)] + forward_gamma[CLAMP((ptr+1)->b - black_b, 0, in_limit)] +
						forward_gamma[CLAMP((ptr+pitch)->b - black_b, 0, in_limit)] + forward_gamma[CLAMP((ptr+pitch+1)->b - black_b, 0, in_limit)];
				ptr->b = inverse_gamma[(unsigned int)CLAMP(val*gain_b, 0, out_limit)];

				val =  forward_gamma[CLAMP(ptr->g - black_g, 0, in_limit)] + forward_gamma[CLAMP((ptr+1)->g - black_g, 0, in_limit)] +
						forward_gamma[CLAMP((ptr+pitch)->g - black_g, 0, in_limit)] + forward_gamma[CLAMP((ptr+pitch+1)->g - black_g, 0, in_limit)];
				ptr->g = inverse_gamma[(unsigned int)CLAMP(val*gain_g, 0, out_limit)];

				val =  forward_gamma[CLAMP(ptr->r - black_r, 0, in_limit)] + forward_gamma[CLAMP((ptr+1)->r - black_r, 0, in_limit)] +
						forward_gamma[CLAMP((ptr+pitch)->r - black_r, 0, in_limit)] + forward_gamma[CLAMP((ptr+pitch+1)->r - black_r, 0, in_limit)];
				ptr->r = inverse_gamma[(unsigned int)CLAMP(val*gain_r, 0, out_limit)];

				count++; // pixel count
				ptr++;  // next pixel, 3 bytes on
			}
		}
	}
	else{  // generic implementation
		gint s = filter->binsize, valr, valg, valb;
		stop_y  = filter->height-s;
		stop_x  = filter->width-s;
		for(y=start_y; y<stop_y; y++){
			ptr = (bgr_pixel *)img_ptr + pitch * y + start_x; // ptr to start of line
			for(x=start_x; x<stop_x; x++){
				valb = valg = valr = 0;
				for(i=0; i<s; i++){
					for(j=0; j<s; j++){
						valb += forward_gamma[CLAMP((ptr+i*pitch+j)->b - black_b, 0, in_limit)];
						valg += forward_gamma[CLAMP((ptr+i*pitch+j)->g - black_g, 0, in_limit)];
						valr += forward_gamma[CLAMP((ptr+i*pitch+j)->r - black_r, 0, in_limit)];
					}
				}

				ptr->b = inverse_gamma[(unsigned int)CLAMP(valb*gain_b, 0, out_limit)];
				ptr->g = inverse_gamma[(unsigned int)CLAMP(valg*gain_g, 0, out_limit)];
				ptr->r = inverse_gamma[(unsigned int)CLAMP(valr*gain_r, 0, out_limit)];

				count++; // pixel count
				ptr++;  // next pixel, 3 bytes on
			}
		}
	}

	gst_buffer_unmap (buf, &minfo);
}

/*
void
gst_bin_image_rgb(Gstbinningfilter *filter, GstBuffer *buf)
{
	gint count=0;
	gint x, y, i, j, val;
	bgr_pixel *ptr=NULL;
	GstMapInfo minfo;

	// ***********************************
	// binning the pixels from 24-bit BGR data
	// to do this in-place, always gather pixels from below and right

	// Access the buffer - READ AND WRITE
	gst_buffer_map (buf, &minfo, GST_MAP_READWRITE);

    gint black_r, black_g, black_b;   // RGB black levels that will be subtracted from each pixel
    gint contrast_r, contrast_g, contrast_b;   // RGB contrast values that will be applied to the summed/binned data

	if(filter->format_is_RGB){  // default is BGR, so swap black and contrast b for r
		black_r = filter->black_b; black_g = filter->black_g; black_b = filter->black_r;
		contrast_r = filter->contrast_b; contrast_g = filter->contrast_g; contrast_b = filter->contrast_r;
	}
	else{
		black_r = filter->black_r; black_g = filter->black_g; black_b = filter->black_b;
		contrast_r = filter->contrast_r; contrast_g = filter->contrast_g; contrast_b = filter->contrast_b;
	}

	gint start_y = 0;
	gint stop_y  = filter->height;
	gint start_x = 0;
	gint stop_x  = filter->width;

	guint8 *img_ptr = minfo.data;
	gint pitch = filter->stride / 3;  // want the number of pixels to next line

	gfloat gain_r = contrast_r / 100.0f;    // convert contrast values into real gain factors, contrast=100 => gain=1 => normal summed binning
	gfloat gain_g = contrast_g / 100.0f;
	gfloat gain_b = contrast_b / 100.0f;

	// Check for the special contrast value (-1) to do averaging rather than binning, gain here is dependent in the bin size.
	if (contrast_r < 0)
		gain_r = 1.0f / (filter->binsize*filter->binsize);
	if (contrast_g < 0)
		gain_g = 1.0f / (filter->binsize*filter->binsize);
	if (contrast_b < 0)
		gain_b = 1.0f / (filter->binsize*filter->binsize);

//	GST_DEBUG_OBJECT (filter, "Gains: %.3f %.3f %.3f, Blacks: %d %d %d", gain_r, gain_g, gain_b, black_r, black_g, black_b);

	if (filter->binsize == 1){  // no binning here but may want to contrast stretch and apply black levels

		if(gain_r==1.0f && gain_g==1.0f && gain_b==1.0f &&
				black_r==0 && black_g==0 && black_b==0){     // Just check that we have to do anything at all, if not return.
			gst_buffer_unmap (buf, &minfo);
			return;
		}

		for(y=start_y; y<stop_y; y++){
			ptr = (bgr_pixel *)img_ptr + pitch * y + start_x; // ptr to start of line
			for(x=start_x; x<stop_x; x++){

				// Use 'val' to limit the result without over or under flowing
				val = ptr->b - black_b;
				ptr->b = MIN(255, MAX(0,val*gain_b));
				val = ptr->g - black_g;
				ptr->g = MIN(255, MAX(0,val*gain_g));
				val = ptr->r - black_r;
				ptr->r = MIN(255, MAX(0,val*gain_r));

				count++; // pixel count
				ptr++;  // next pixel, 3 bytes on
			}
		}
	}
	else if (filter->binsize == 2){  // fast implementation for 2x2
		stop_y  = filter->height-1;
		stop_x  = filter->width-1;
		for(y=start_y; y<stop_y; y++){
			ptr = (bgr_pixel *)img_ptr + pitch * y + start_x; // ptr to start of line
			for(x=start_x; x<stop_x; x++){

				// Use 'val' to limit the result without over or under flowing
				val = ptr->b + (ptr+1)->b + (ptr+pitch)->b + (ptr+pitch+1)->b - 4*black_b;
				ptr->b = MIN(255, MAX(0,val*gain_b));
				val = ptr->g + (ptr+1)->g + (ptr+pitch)->g + (ptr+pitch+1)->g - 4*black_g;
				ptr->g = MIN(255, MAX(0,val*gain_g));
				val = ptr->r + (ptr+1)->r + (ptr+pitch)->r + (ptr+pitch+1)->r - 4*black_r;
				ptr->r = MIN(255, MAX(0,val*gain_r));

				count++; // pixel count
				ptr++;  // next pixel, 3 bytes on
			}
		}
	}
	else if (filter->binsize == 3){  // fast implementation for 3x3
		stop_y  = filter->height-2;
		stop_x  = filter->width-2;
		for(y=start_y; y<stop_y; y++){
			ptr = (bgr_pixel *)img_ptr + pitch * y + start_x; // ptr to start of line
			for(x=start_x; x<stop_x; x++){

				// Use 'val' to limit the result without over or under flowing
				val = ptr->b + (ptr+1)->b  + (ptr+2)->b +
						(ptr+pitch)->b + (ptr+pitch+1)->b + (ptr+pitch+2)->b +
						(ptr+2*pitch)->b + (ptr+2*pitch+1)->b + (ptr+2*pitch+2)->b - 9*black_b;
				ptr->b = MIN(255, MAX(0,val*gain_b));
				val = ptr->g + (ptr+1)->g  + (ptr+2)->g +
						(ptr+pitch)->g + (ptr+pitch+1)->g + (ptr+pitch+2)->g +
						(ptr+2*pitch)->g + (ptr+2*pitch+1)->g + (ptr+2*pitch+2)->g - 9*black_g;
				ptr->g = MIN(255, MAX(0,val*gain_g));
				val = ptr->r + (ptr+1)->r  + (ptr+2)->r +
						(ptr+pitch)->r + (ptr+pitch+1)->r + (ptr+pitch+2)->r +
						(ptr+2*pitch)->r + (ptr+2*pitch+1)->r + (ptr+2*pitch+2)->r - 9*black_r;
				ptr->r = MIN(255, MAX(0,val*gain_r));

				count++; // pixel count
				ptr++;  // next pixel, 3 bytes on
			}
		}
	}
	else if (filter->binsize == 4){  // fast implementation for 4x4
		stop_y  = filter->height-3;
		stop_x  = filter->width-3;
		for(y=start_y; y<stop_y; y++){
			ptr = (bgr_pixel *)img_ptr + pitch * y + start_x; // ptr to start of line
			for(x=start_x; x<stop_x; x++){

				// Use 'val' to limit the result without over or under flowing
				val = ptr->b + (ptr+1)->b  + (ptr+2)->b + (ptr+3)->b +
						(ptr+pitch)->b + (ptr+pitch+1)->b + (ptr+pitch+2)->b + (ptr+pitch+3)->b +
						(ptr+2*pitch)->b + (ptr+2*pitch+1)->b + (ptr+2*pitch+2)->b + (ptr+2*pitch+3)->b +
						(ptr+3*pitch)->b + (ptr+3*pitch+1)->b + (ptr+3*pitch+2)->b + (ptr+3*pitch+3)->b + - 16*black_b;
				ptr->b = MIN(255, MAX(0,val*gain_b));
				val = ptr->g + (ptr+1)->g  + (ptr+2)->g + (ptr+3)->g +
						(ptr+pitch)->g + (ptr+pitch+1)->g + (ptr+pitch+2)->g + (ptr+pitch+3)->g +
						(ptr+2*pitch)->g + (ptr+2*pitch+1)->g + (ptr+2*pitch+2)->g + (ptr+2*pitch+3)->g +
						(ptr+3*pitch)->g + (ptr+3*pitch+1)->g + (ptr+3*pitch+2)->g + (ptr+3*pitch+3)->g + - 16*black_g;
				ptr->g = MIN(255, MAX(0,val*gain_g));
				val = ptr->r + (ptr+1)->r  + (ptr+2)->r + (ptr+3)->r +
						(ptr+pitch)->r + (ptr+pitch+1)->r + (ptr+pitch+2)->r + (ptr+pitch+3)->r +
						(ptr+2*pitch)->r + (ptr+2*pitch+1)->r + (ptr+2*pitch+2)->r + (ptr+2*pitch+3)->r +
						(ptr+3*pitch)->r + (ptr+3*pitch+1)->r + (ptr+3*pitch+2)->r + (ptr+3*pitch+3)->r + - 16*black_r;
				ptr->r = MIN(255, MAX(0,val*gain_r));

				count++; // pixel count
				ptr++;  // next pixel, 3 bytes on
			}
		}
	}
	else{  // generic implementation
		gint s = filter->binsize, valr, valg, valb;
		stop_y  = filter->height-s;
		stop_x  = filter->width-s;
		for(y=start_y; y<stop_y; y++){
			ptr = (bgr_pixel *)img_ptr + pitch * y + start_x; // ptr to start of line
			for(x=start_x; x<stop_x; x++){
				valb = valg = valr = 0;
				for(i=0; i<s; i++){
					for(j=0; j<s; j++){
						valb += (ptr+i*pitch+j)->b;
						valg += (ptr+i*pitch+j)->g;
						valr += (ptr+i*pitch+j)->r;
					}
				}

				valb -= s*s*black_b;
				valg -= s*s*black_g;
				valr -= s*s*black_r;

				ptr->b = MIN(255, MAX(0,valb*gain_b));
				ptr->g = MIN(255, MAX(0,valg*gain_g));
				ptr->r = MIN(255, MAX(0,valr*gain_r));

				count++; // pixel count
				ptr++;  // next pixel, 3 bytes on
			}
		}
	}

	gst_buffer_unmap (buf, &minfo);
}
*/


void
gst_binningfilter_rgb_init(void)
{
	GST_DEBUG_CATEGORY_INIT (gst_binningfilter_RGB_debug, "binningfilter",
			1, "binningfilter RGB");
}

