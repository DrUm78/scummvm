/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/rendermode.h"

#include "common/gui_options.h"
#include "common/str.h"
#include "common/translation.h"


namespace Common {


const RenderModeDescription g_renderModes[] = {
	// I18N: Hercules is graphics card name
	{ "hercGreen", _s("Hercules Green"), kRenderHercG },
	{ "hercAmber", _s("Hercules Amber"), kRenderHercA },
	{ "cga", "CGA", kRenderCGA },
	{ "cgaComp", "CGA Composite", kRenderCGAComp },
	// I18N: CGA black-and-white
	{ "cgaBW", "CGA b/w", kRenderCGA_BW },
	{ "ega", "EGA", kRenderEGA },
	{ "vga", "VGA", kRenderVGA },
	{ "amiga", "Amiga", kRenderAmiga },
	{ "fmtowns", "FM-TOWNS", kRenderFMTowns },
	{ "pc9821", _s("PC-9821 (256 Colors)"), kRenderPC9821 },
	{ "pc9801", _s("PC-9801 (16 Colors)"), kRenderPC9801 },
	{ "2gs", "Apple IIgs", kRenderApple2GS },
	{ "atari", "Atari ST", kRenderAtariST },
	{ "macintosh", "Macintosh", kRenderMacintosh },
	// I18N: Macintosh black-and-white
	{ "macintoshbw", _s("Macintosh b/w"), kRenderMacintoshBW },
	{ "zx", "ZX Spectrum", kRenderZX },
	{nullptr, nullptr, kRenderDefault}
};

struct RenderGUIOMapping {
	RenderMode id;
	const char *guio;
};

// TODO: Merge s_renderGUIOMapping into g_renderModes? the kRenderDefault
// could be used to indicate "any" mode when passed to renderMode2GUIO (if
// we wanted to merge allRenderModesGUIOs back into)
static const RenderGUIOMapping s_renderGUIOMapping[] = {
	{ kRenderHercG,			GUIO_RENDERHERCGREEN },
	{ kRenderHercA,			GUIO_RENDERHERCAMBER },
	{ kRenderCGA,		    GUIO_RENDERCGA },
	{ kRenderEGA,		    GUIO_RENDEREGA },
	{ kRenderVGA,			GUIO_RENDERVGA },
	{ kRenderAmiga,			GUIO_RENDERAMIGA },
	{ kRenderFMTowns,		GUIO_RENDERFMTOWNS },
	{ kRenderPC9821,		GUIO_RENDERPC9821 },
	{ kRenderPC9801,		GUIO_RENDERPC9801 },
	{ kRenderApple2GS,		GUIO_RENDERAPPLE2GS },
	{ kRenderAtariST,		GUIO_RENDERATARIST },
	{ kRenderMacintosh,		GUIO_RENDERMACINTOSH },
	{ kRenderMacintoshBW,	GUIO_RENDERMACINTOSHBW },
	{ kRenderCGAComp,	    GUIO_RENDERCGACOMP },
	{ kRenderCGA_BW,	    GUIO_RENDERCGABW }
};

DECLARE_TRANSLATION_ADDITIONAL_CONTEXT("Hercules Green", "lowres")
DECLARE_TRANSLATION_ADDITIONAL_CONTEXT("Hercules Amber", "lowres")

RenderMode parseRenderMode(const String &str) {
	if (str.empty())
		return kRenderDefault;

	const RenderModeDescription *l = g_renderModes;
	for (; l->code; ++l) {
		if (str.equalsIgnoreCase(l->code))
			return l->id;
	}

	return kRenderDefault;
}

const char *getRenderModeCode(RenderMode id) {
	const RenderModeDescription *l = g_renderModes;
	for (; l->code; ++l) {
		if (l->id == id)
			return l->code;
	}
	return nullptr;
}

const char *getRenderModeDescription(RenderMode id) {
	const RenderModeDescription *l = g_renderModes;
	for (; l->code; ++l) {
		if (l->id == id)
			return l->description;
	}
	return nullptr;
}

String renderMode2GUIO(RenderMode id) {
	String res;

	for (int i = 0; i < ARRAYSIZE(s_renderGUIOMapping); i++) {
		if (id == s_renderGUIOMapping[i].id)
			res += s_renderGUIOMapping[i].guio;
	}

	return res;
}

String allRenderModesGUIOs() {
	String res;

	for (int i = 0; i < ARRAYSIZE(s_renderGUIOMapping); i++) {
		res += s_renderGUIOMapping[i].guio;
	}

	return res;
}

} // End of namespace Common
