/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / RECOIL decoder filter, based on RECOIL
 *  (https://recoil.sourceforge.net/) - "Retro Computer Image Library", which
 *  covers several hundred 8/16-bit picture formats: Amiga IFF/ILBM, Atari ST
 *  Degas (PI1/PC1 and friends), C64, ZX Spectrum, Apple II and so on.
 *
 *  RECOIL dispatches on the file *extension*, not on a magic number, so the
 *  filter forwards the source file name to it (see recoil_filename below).
 */

#include <gpac/filters.h>
#include <gpac/constants.h>
#include <string.h>
#include <stdlib.h>

#include <recoil.h>

typedef struct
{
	GF_FilterPid *ipid, *opid;
	Bool is_playing;
} GF_RECOILDecCtx;

/* RECOIL only looks at the extension, but it needs a name to look at: take the
 * source file name when the demuxer exposes one, and fall back to a synthetic
 * "image.<ext>" built from the file-extension property otherwise. */
static void recoil_filename(GF_FilterPid *pid, char *buf, u32 buf_size)
{
	const GF_PropertyValue *p;

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_FILEPATH);
	if (!p)
		p = gf_filter_pid_get_property(pid, GF_PROP_PID_URL);
	if (p && p->value.string)
	{
		strncpy(buf, p->value.string, buf_size - 1);
		buf[buf_size - 1] = 0;
		return;
	}

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_FILE_EXT);
	if (p && p->value.string)
		snprintf(buf, buf_size, "image.%s", p->value.string);
	else
		snprintf(buf, buf_size, "image.iff");
}

static GF_Err recoildec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	GF_RECOILDecCtx *ctx = (GF_RECOILDecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	ctx->ipid = pid;
	gf_filter_pid_set_framing_mode(pid, GF_TRUE);

	if (!ctx->opid)
		ctx->opid = gf_filter_pid_new(filter);

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_VISUAL));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));

	return GF_OK;
}

static Bool recoildec_process_event(GF_Filter *filter, const GF_FilterEvent *evt)
{
	GF_RECOILDecCtx *ctx = (GF_RECOILDecCtx *)gf_filter_get_udta(filter);
	switch (evt->base.type)
	{
	case GF_FEVT_PLAY:
		ctx->is_playing = GF_TRUE;
		return GF_FALSE;
	case GF_FEVT_STOP:
		ctx->is_playing = GF_FALSE;
		return GF_FALSE;
	default:
		return GF_FALSE;
	}
}

static GF_Err recoildec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output;
	u32 size, out_size, i, nb_pixels;
	int width, height;
	const int *pixels;
	RECOIL *recoil;
	char filename[GF_MAX_PATH];
	GF_RECOILDecCtx *ctx = (GF_RECOILDecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);
	if (!data)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}

	recoil = RECOIL_New();
	if (!recoil)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}

	recoil_filename(ctx->ipid, filename, sizeof(filename));

	if (!RECOIL_Decode(recoil, filename, data, (int)size))
	{
		RECOIL_Delete(recoil);
		gf_filter_pid_drop_packet(ctx->ipid);
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[RECOILDec] Failed to decode %s\n", filename));
		return GF_NON_COMPLIANT_BITSTREAM;
	}
	gf_filter_pid_drop_packet(ctx->ipid);

	width = RECOIL_GetWidth(recoil);
	height = RECOIL_GetHeight(recoil);
	pixels = RECOIL_GetPixels(recoil);
	if (width <= 0 || height <= 0 || !pixels)
	{
		RECOIL_Delete(recoil);
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	nb_pixels = (u32)width * (u32)height;
	out_size = nb_pixels * 3;

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT((u32)width));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT((u32)height));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STRIDE, &PROP_UINT((u32)width * 3));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGB));

	dst_pck = gf_filter_pck_new_alloc(ctx->opid, out_size, &output);
	if (!dst_pck)
	{
		RECOIL_Delete(recoil);
		return GF_OUT_OF_MEM;
	}

	/* RECOIL hands back one 0xRRGGBB integer per pixel, top-down. */
	for (i = 0; i < nb_pixels; i++)
	{
		u32 rgb = (u32)pixels[i];
		output[i * 3] = (u8)((rgb >> 16) & 0xFF);
		output[i * 3 + 1] = (u8)((rgb >> 8) & 0xFF);
		output[i * 3 + 2] = (u8)(rgb & 0xFF);
	}
	RECOIL_Delete(recoil);

	gf_filter_pck_set_cts(dst_pck, 0);
	gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
	gf_filter_pck_send(dst_pck);

	gf_filter_pid_set_eos(ctx->opid);
	return GF_EOS;
}

static void recoildec_finalize(GF_Filter *filter)
{
}

/* Only the formats this repository ships test signals for are declared; RECOIL
 * itself recognises several hundred extensions, more can be added here. */
static const GF_FilterCapability RECOILDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "iff|ilbm|lbm|pi1|pi2|pi3|pc1|pc2|pc3|neo|doo|tny|ham|acbm"),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_MIME, "image/x-ilbm|image/x-iff|image/x-degas"),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister RECOILDecoderRegister = {
	.name = "recoildec",
	GF_FS_SET_DESCRIPTION("Retro computer image decoder (IFF/ILBM, Degas, ...)")
		GF_FS_SET_HELP("This filter decodes retro computer picture formats (Amiga IFF/ILBM, Atari ST Degas PI1/PC1, and many others) using RECOIL.")
			.private_size = sizeof(GF_RECOILDecCtx),
	SETCAPS(RECOILDecCaps),
	.configure_pid = recoildec_configure_pid,
	.process = recoildec_process,
	.process_event = recoildec_process_event,
	.finalize = recoildec_finalize,
};

const GF_FilterRegister *EMSCRIPTEN_KEEPALIVE recoildec_register(GF_FilterSession *session)
{
	return &RECOILDecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_recoildec(void) {
    gf_filter_auto_register("recoildec", recoildec_register);
}
