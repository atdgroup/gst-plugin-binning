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
 * gst-launch-1.0 videotestsrc ! videoconvert ! binningfilter binsize=2 ! videoconvert ! xvimagesink
 * gst-launch-1.0 videotestsrc ! videoconvert ! binningfilter binsize=2 resize=true ! videocrop right=160 bottom=120 ! videoconvert ! xvimagesink
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

GST_DEBUG_CATEGORY_STATIC (gst_binningfilter_debug);
#define GST_CAT_DEFAULT gst_binningfilter_debug

/* Filter signals and args */
enum
{
	/* FILL ME */
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_ALGORITHM,
	PROP_BINSIZE,
	PROP_RESIZE,
	PROP_RBLACK,
	PROP_GBLACK,
	PROP_BBLACK,
	PROP_RCONTRAST,
	PROP_GCONTRAST,
	PROP_BCONTRAST
};

#define DEFAULT_PROP_ALGORITHM PROP_RGB
#define DEFAULT_PROP_BINSIZE 1
#define DEFAULT_PROP_RESIZE FALSE
#define DEFAULT_PROP_RBLACK 0
#define DEFAULT_PROP_GBLACK 0
#define DEFAULT_PROP_BBLACK 0
#define DEFAULT_PROP_RCONTRAST 100
#define DEFAULT_PROP_GCONTRAST 100
#define DEFAULT_PROP_BCONTRAST 100

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
				("{ BGR, RGB }"))
);

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
				("{ BGR, RGB }"))
);

#define gst_binningfilter_parent_class parent_class
G_DEFINE_TYPE (Gstbinningfilter, gst_binningfilter, GST_TYPE_ELEMENT);

static void gst_binningfilter_set_property (GObject * object, guint prop_id,
		const GValue * value, GParamSpec * pspec);
static void gst_binningfilter_get_property (GObject * object, guint prop_id,
		GValue * value, GParamSpec * pspec);

static gboolean gst_binningfilter_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn gst_binningfilter_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);
static void gst_binningfilter_finalize (GObject * object);

void
create_gamma_lut(Gstbinningfilter *filter)
{
	unsigned int i;
	double invgamma = 1.0/GAMMA;

	filter->forward_gamma = g_new(double, IN_RANGE);
	filter->inverse_gamma = g_new(unsigned int, OUT_RANGE);

//	GST_DEBUG_OBJECT (filter, "create_gamma_lut NOW !!!!!!!!!");

	for (i=0;i<IN_RANGE;i++){
		filter->forward_gamma[i] = (double)((double)OUT_RANGE * pow(((double)i/(double)FACTOR) + OFFSET, (double)GAMMA));
//		filter->forward_gamma[i] = (double)((double)OUT_RANGE * pow(((double)i/(double)IN_RANGE), (double)GAMMA));
//		GST_DEBUG_OBJECT (filter, "forward_gamma: %d - %d", i, filter->forward_gamma[i]);
	}

    // NB Not applying the output offset, OFFSET, here since not adding a linear portion to the gamma curve (see flycapsrc LUT))!!! TODO: Is this OK?
	for (i=0;i<OUT_RANGE;i++){
		filter->inverse_gamma[i] = (unsigned int)(IN_RANGE * pow(((double)i/OUT_RANGE), invgamma));
//		GST_DEBUG_OBJECT (filter, "inverse_gamma: %d - %d", i, filter->inverse_gamma[i]);
	}
}

#define TYPE_BUNNINGTYPE (binningtype_get_type ())
static GType
binningtype_get_type (void)
{
  static GType binningtype_type = 0;

  if (!binningtype_type) {
    static GEnumValue binningtype_types[] = {
	  { PROP_RGB, "Bin R, G and B channels separately.", "rgb" },
	  { PROP_CHROMA,  "Bin Luminance, maintaining chroma difference signal (B-G and R-G).", "chroma"  },
	  { PROP_TEST,  "Switch between all options.", "test"  },
      { 0, NULL, NULL },
    };

    binningtype_type =
	g_enum_register_static ("BinningTypeType", binningtype_types);
  }

  return binningtype_type;
}


/* GObject vmethod implementations */

/* initialize the binningfilter's class */
static void
gst_binningfilter_class_init (GstbinningfilterClass * klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	gobject_class->set_property = gst_binningfilter_set_property;
	gobject_class->get_property = gst_binningfilter_get_property;
	gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_binningfilter_finalize);

	// binning type property
	g_object_class_install_property (gobject_class, PROP_ALGORITHM,
			g_param_spec_enum("algorithm", "Binning algorithm.", "Algorithm to use.", TYPE_BUNNINGTYPE, DEFAULT_PROP_ALGORITHM,
					(GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

	// binsize property
	g_object_class_install_property (gobject_class, PROP_BINSIZE,
	  g_param_spec_int("binsize", "Bin size.", "Pixel data will be combined over the area binsize x binsize.", 1, 7, DEFAULT_PROP_BINSIZE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
	g_object_class_install_property (gobject_class, PROP_RESIZE,
	  g_param_spec_boolean("resize", "Re-size.", "Resize the image as binning is performed. Makes a resized image in the top-left of the buffer, use a videocrop element to remove unwanted area. Only valid for rgb binning.", DEFAULT_PROP_RESIZE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

	// Black level properties
	g_object_class_install_property (gobject_class, PROP_RBLACK,
	  g_param_spec_int("rblack", "Red Black Level.", "Will be subtracted from all red pixel values.", 0, 255, DEFAULT_PROP_RBLACK,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
	g_object_class_install_property (gobject_class, PROP_GBLACK,
	  g_param_spec_int("gblack", "Green Black Level.", "Will be subtracted from all green pixel values.", 0, 255, DEFAULT_PROP_GBLACK,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
	g_object_class_install_property (gobject_class, PROP_BBLACK,
	  g_param_spec_int("bblack", "Blue Black Level.", "Will be subtracted from all blue pixel values.", 0, 255, DEFAULT_PROP_BBLACK,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

	// Contrast properties
	g_object_class_install_property (gobject_class, PROP_RCONTRAST,
	  g_param_spec_int("rcontrast", "Red Contrast.", "A gain of rcontrast/100 will be applied to all red pixel binned values. Use rcontrast = 100 for normal pixel summation, -1 for averaging.", -1, 1000, DEFAULT_PROP_RCONTRAST,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
	g_object_class_install_property (gobject_class, PROP_GCONTRAST,
	  g_param_spec_int("gcontrast", "Green Contrast.", "A gain of gcontrast/100 will be applied to all green pixel binned values. Use gcontrast = 100 for normal pixel summation, -1 for averaging.", -1, 1000, DEFAULT_PROP_GCONTRAST,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
	g_object_class_install_property (gobject_class, PROP_BCONTRAST,
	  g_param_spec_int("bcontrast", "Blue Contrast.", "A gain of bcontrast/100 will be applied to all blue pixel binned values. Use bcontrast = 100 for normal pixel summation, -1 for averaging.", -1, 1000, DEFAULT_PROP_BCONTRAST,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));

	gst_element_class_set_details_simple(gstelement_class,
			"binningfilter",
			"Filter",
			"Make binning on the image. Add local pixels together.",
			"Paul R Barber <<paul.barber@oncology.ox.ac.uk>>");

	gst_element_class_add_pad_template (gstelement_class,
			gst_static_pad_template_get (&src_factory));
	gst_element_class_add_pad_template (gstelement_class,
			gst_static_pad_template_get (&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_binningfilter_init (Gstbinningfilter * filter)
{
	filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
	gst_pad_set_event_function (filter->sinkpad,
			GST_DEBUG_FUNCPTR(gst_binningfilter_sink_event));
	gst_pad_set_chain_function (filter->sinkpad,
			GST_DEBUG_FUNCPTR(gst_binningfilter_chain));
	GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
	gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

	filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
	GST_PAD_SET_PROXY_CAPS (filter->srcpad);
	gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

	filter->format_is_RGB = FALSE;

	filter->algorithm = DEFAULT_PROP_ALGORITHM;
	filter->binsize = DEFAULT_PROP_BINSIZE;
	filter->resize = DEFAULT_PROP_RESIZE;

	filter->black_r = DEFAULT_PROP_RBLACK;
	filter->black_g = DEFAULT_PROP_GBLACK;
	filter->black_b = DEFAULT_PROP_BBLACK;

	filter->contrast_r = DEFAULT_PROP_RCONTRAST;
	filter->contrast_g = DEFAULT_PROP_GCONTRAST;
	filter->contrast_b = DEFAULT_PROP_BCONTRAST;

	create_gamma_lut(filter);
}

static void
gst_binningfilter_finalize (GObject * object)
{
	Gstbinningfilter *filter = GST_BINNINGFILTER (object);

	g_free(filter->forward_gamma);
	filter->forward_gamma = NULL;

	g_free(filter->inverse_gamma);
	filter->inverse_gamma = NULL;

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_binningfilter_set_property (GObject * object, guint prop_id,
		const GValue * value, GParamSpec * pspec)
{
	Gstbinningfilter *filter = GST_BINNINGFILTER (object);

	switch (prop_id) {
	case PROP_ALGORITHM:
		filter->algorithm = g_value_get_enum (value);
		break;
	case PROP_BINSIZE:
		filter->binsize = g_value_get_int (value);
		break;
	case PROP_RESIZE:
		filter->resize = g_value_get_boolean(value);
		break;
	case PROP_RBLACK:
		filter->black_r = g_value_get_int (value);
		break;
	case PROP_GBLACK:
		filter->black_g = g_value_get_int (value);
		break;
	case PROP_BBLACK:
		filter->black_b = g_value_get_int (value);
		break;
	case PROP_RCONTRAST:
		filter->contrast_r = g_value_get_int (value);
		break;
	case PROP_GCONTRAST:
		filter->contrast_g = g_value_get_int (value);
		break;
	case PROP_BCONTRAST:
		filter->contrast_b = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gst_binningfilter_get_property (GObject * object, guint prop_id,
		GValue * value, GParamSpec * pspec)
{
	Gstbinningfilter *filter = GST_BINNINGFILTER (object);

	switch (prop_id) {
	case PROP_ALGORITHM:
		g_value_set_enum(value, filter->algorithm);
		break;
	case PROP_BINSIZE:
		g_value_set_int (value, filter->binsize);
		break;
	case PROP_RESIZE:
		g_value_set_boolean(value, filter->resize);
		break;
	case PROP_RBLACK:
		if(!filter->format_is_RGB)
			g_value_set_int (value, filter->black_r);
		else
			g_value_set_int (value, filter->black_b);	 // if RGB format data reverse r and b
		break;
	case PROP_GBLACK:
		g_value_set_int (value, filter->black_g);
		break;
	case PROP_BBLACK:
		if(!filter->format_is_RGB)
			g_value_set_int (value, filter->black_b);
		else
			g_value_set_int (value, filter->black_r);	 // if RGB format data reverse r and b
		break;
	case PROP_RCONTRAST:
		if(!filter->format_is_RGB)
			g_value_set_int (value, filter->contrast_r);
		else
			g_value_set_int (value, filter->contrast_b);	 // if RGB format data reverse r and b
		break;
	case PROP_GCONTRAST:
		g_value_set_int (value, filter->contrast_g);
		break;
	case PROP_BCONTRAST:
		if(!filter->format_is_RGB)
			g_value_set_int (value, filter->contrast_b);
		else
			g_value_set_int (value, filter->contrast_r);	 // if RGB format data reverse r and b
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean
gst_binningfilter_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
	gboolean ret;
	Gstbinningfilter *filter;
	gchar *format;

	filter = GST_BINNINGFILTER (parent);

	switch (GST_EVENT_TYPE (event)) {
	case GST_EVENT_CAPS:
	{
		GstCaps *caps;
		GstStructure *structure;

		gst_event_parse_caps (event, &caps);
		/* do something with the caps */

		if(gst_caps_is_fixed (caps)){

			structure = gst_caps_get_structure (caps, 0);
			if (!gst_structure_get_int (structure, "width", &(filter->width)) ||
					!gst_structure_get_int (structure, "height", &(filter->height))) {
				GST_ERROR_OBJECT (filter, "No width/height available\n");
			}

			filter->stride = filter->width * 3;  // TODO Not sure how to get this properly, but we have BGR or RGB data

			format = gst_structure_get_string (structure, "format");
			if (!format) {
				GST_ERROR_OBJECT (filter, "No format available\n");
			}

			if (strcmp(format, "RGB")==0){
				filter->format_is_RGB = TRUE;
				GST_DEBUG_OBJECT (filter, "Format is RGB");
			}

			GST_DEBUG_OBJECT (filter, "The video size of this set of capabilities is %dx%d, %d\n",
					filter->width, filter->height, filter->stride);
		}
		else {
			GST_ERROR_OBJECT (filter, "Caps not fixed.\n");
		}

		/* and forward */
		ret = gst_pad_event_default (pad, parent, event);
		break;
	}
	case GST_EVENT_EOS:

		/* and forward */
		ret = gst_pad_event_default (pad, parent, event);
		break;
	default:
			ret = gst_pad_event_default (pad, parent, event);
			break;
	}
	return ret;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_binningfilter_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
	Gstbinningfilter *filter;

	filter = GST_BINNINGFILTER (parent);

	// Process image
	switch (filter->algorithm) {
	case PROP_RGB:
	default:
		if(!filter->resize)
			gst_bin_image_rgb(filter, buf);
		else
			gst_bin_resize_image_rgb(filter, buf);
		break;
	case PROP_CHROMA:
		gst_bin_image_chroma(filter, buf);
		break;
	case PROP_TEST:
		if (GST_TIME_AS_SECONDS(buf->pts)%2){   // every second switch the algorithm
			GST_DEBUG_OBJECT (filter, "%d Binning algorithm: rgb\n", (int)GST_TIME_AS_SECONDS(buf->pts));
			gst_bin_image_rgb(filter, buf);
		}
		else{
			GST_DEBUG_OBJECT (filter, "%d Binning algorithm: chroma\n", (int)GST_TIME_AS_SECONDS(buf->pts));
			gst_bin_image_chroma(filter, buf);
		}
		break;
	}

	// push out the changed buffer
	return gst_pad_push (filter->srcpad, buf);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
binningfilter_init (GstPlugin * plugin)
{
	/* debug category for fltering log messages
	 *
	 * exchange the string 'Template binningfilter' with your description
	 */
	GST_DEBUG_CATEGORY_INIT (gst_binningfilter_debug, "binningfilter",
			1, "Template binningfilter");

	  gst_binningfilter_rgb_init();
	  gst_binningfilter_rgbresize_init();
	  gst_binningfilter_chroma_init();

	  if (gst_element_register (plugin, "binningfilter", GST_RANK_NONE,
				GST_TYPE_BINNINGFILTER))
	    return TRUE;

	  return FALSE;

}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "binningfilter"
#endif

/* gstreamer looks for this structure to register binningfilters
 *
 * exchange the string 'Template binningfilter' with your binningfilter description
 */
GST_PLUGIN_DEFINE (
		GST_VERSION_MAJOR,
		GST_VERSION_MINOR,
		binningfilter,
		"This element performs binning on the image, e.g. adding pixel values together. If the contrast values are -1 then averaging will be performed (i.e. box averaging low-pass filter)",
		binningfilter_init,
		VERSION,
		"LGPL",
		PACKAGE_NAME,
		"http://users.ox.ac.uk/~atdgroup"
)
