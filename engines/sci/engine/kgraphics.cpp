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

#include "common/system.h"
#include "common/translation.h"

#include "engines/util.h"
#include "graphics/cursorman.h"
#include "graphics/surface.h"

#include "gui/message.h"

#include "sci/sci.h"
#include "sci/event.h"
#include "sci/resource/resource.h"
#include "sci/engine/features.h"
#include "sci/engine/guest_additions.h"
#include "sci/engine/savegame.h"
#include "sci/engine/state.h"
#include "sci/engine/selector.h"
#include "sci/engine/tts.h"
#include "sci/engine/kernel.h"
#include "sci/graphics/animate.h"
#include "sci/graphics/cache.h"
#include "sci/graphics/compare.h"
#include "sci/graphics/controls16.h"
#include "sci/graphics/cursor.h"
#include "sci/graphics/palette.h"
#include "sci/graphics/paint16.h"
#include "sci/graphics/picture.h"
#include "sci/graphics/ports.h"
#include "sci/graphics/remap.h"
#include "sci/graphics/screen.h"
#include "sci/graphics/text16.h"
#include "sci/graphics/view.h"
#ifdef ENABLE_SCI32
#include "sci/graphics/text32.h"
#endif

namespace Sci {

static int16 adjustGraphColor(int16 color) {
	// WORKAROUND: EGA and Amiga games can set invalid colors (above 0 - 15).
	// It seems only the lower nibble was used in these games.
	// bug #5267, #5968.
	// Confirmed in EGA games KQ4(late), QFG1(ega), LB1 that
	// at least FillBox (only one of the functions using adjustGraphColor)
	// behaves like this.
	if (g_sci->getResMan()->getViewType() == kViewEga)
		return color & 0x0F;	// 0 - 15
	else
		return color;
}

int showScummVMDialog(const Common::U32String &message, const Common::U32String &altButton = Common::U32String(), bool alignCenter = true) {
	Graphics::TextAlign alignment = alignCenter ? Graphics::kTextAlignCenter : Graphics::kTextAlignLeft;
	GUI::MessageDialog dialog(message, _("OK"), altButton, alignment);
	return dialog.runModal();
}

void kDirLoopWorker(reg_t object, uint16 angle, EngineState *s, int argc, reg_t *argv) {
	GuiResourceId viewId = readSelectorValue(s->_segMan, object, SELECTOR(view));
	uint16 signal = readSelectorValue(s->_segMan, object, SELECTOR(signal));

	if (signal & kSignalDoesntTurn)
		return;

	int16 useLoop = -1;
	if (getSciVersion() > SCI_VERSION_0_EARLY) {
		if ((angle > 315) || (angle < 45)) {
			useLoop = 3;
		} else if ((angle > 135) && (angle < 225)) {
			useLoop = 2;
		}
	} else {
		// SCI0EARLY
		if ((angle > 330) || (angle < 30)) {
			useLoop = 3;
		} else if ((angle > 150) && (angle < 210)) {
			useLoop = 2;
		}
	}
	if (useLoop == -1) {
		if (angle >= 180) {
			useLoop = 1;
		} else {
			useLoop = 0;
		}
	} else {
		int16 loopCount = g_sci->_gfxCache->kernelViewGetLoopCount(viewId);
		if (loopCount < 4)
			return;
	}

	writeSelectorValue(s->_segMan, object, SELECTOR(loop), useLoop);
}

static reg_t kSetCursorSci0(EngineState *s, int argc, reg_t *argv) {
	Common::Point pos;
	GuiResourceId cursorId = argv[0].toSint16();

	// Set pointer position, if requested
	if (argc >= 4) {
		pos.y = argv[3].toSint16();
		pos.x = argv[2].toSint16();
		g_sci->_gfxCursor->kernelSetPos(pos);
	}

	if ((argc >= 2) && (argv[1].toSint16() == 0)) {
		cursorId = -1;
	}

	g_sci->_gfxCursor->kernelSetShape(cursorId);
	return s->r_acc;
}

static reg_t kSetCursorSci11(EngineState *s, int argc, reg_t *argv) {
	Common::Point pos;
	Common::Point *hotspot = nullptr;

	switch (argc) {
	case 1:
		switch (argv[0].toSint16()) {
		case 0:
			g_sci->_gfxCursor->kernelHide();
			break;
		case -1:
			g_sci->_gfxCursor->kernelClearZoomZone();
			break;
		case -2:
			g_sci->_gfxCursor->kernelResetMoveZone();
			break;
		default:
			g_sci->_gfxCursor->kernelShow();
			break;
		}
		break;
	case 2:
		pos.y = argv[1].toSint16();
		pos.x = argv[0].toSint16();

		g_sci->_gfxCursor->kernelSetPos(pos);
		break;
	case 4: {
		int16 top, left, bottom, right;

		if (getSciVersion() >= SCI_VERSION_2) {
			top = argv[1].toSint16();
			left = argv[0].toSint16();
			bottom = argv[3].toSint16();
			right = argv[2].toSint16();
		} else {
			top = argv[0].toSint16();
			left = argv[1].toSint16();
			bottom = argv[2].toSint16();
			right = argv[3].toSint16();
		}
		// bottom/right needs to be included into our movezone, because we compare it like any regular Common::Rect
		bottom++;
		right++;

		if ((right >= left) && (bottom >= top)) {
			Common::Rect rect = Common::Rect(left, top, right, bottom);
			g_sci->_gfxCursor->kernelSetMoveZone(rect);
		} else {
			warning("kSetCursor: Ignoring invalid mouse zone (%i, %i)-(%i, %i)", left, top, right, bottom);
		}
		break;
	}
	case 9: // case for kq5cd, we are getting calling with 4 additional 900d parameters
	case 5:
		hotspot = new Common::Point(argv[3].toSint16(), argv[4].toSint16());
		// Fallthrough
	case 3:
		if (g_sci->getPlatform() == Common::kPlatformMacintosh) {
			delete hotspot; // Mac cursors have their own hotspot, so ignore any we get here
			g_sci->_gfxCursor->kernelSetMacCursor(argv[0].toUint16(), argv[1].toUint16(), argv[2].toUint16());
		} else {
			g_sci->_gfxCursor->kernelSetView(argv[0].toUint16(), argv[1].toUint16(), argv[2].toUint16(), hotspot);
		}
		break;
	case 10:
		// Freddy pharkas, when using the whiskey glass to read the prescription (bug #4969)
		g_sci->_gfxCursor->kernelSetZoomZone(argv[0].toUint16(),
			Common::Rect(argv[1].toUint16(), argv[2].toUint16(), argv[3].toUint16(), argv[4].toUint16()),
			argv[5].toUint16(), argv[6].toUint16(), argv[7].toUint16(),
			argv[8].toUint16(), argv[9].toUint16());
		break;
	default :
		error("kSetCursor: Unhandled case: %d arguments given", argc);
		break;
	}
	return s->r_acc;
}

reg_t kSetCursor(EngineState *s, int argc, reg_t *argv) {
	switch (g_sci->_features->detectSetCursorType()) {
	case SCI_VERSION_0_EARLY:
		return kSetCursorSci0(s, argc, argv);
	case SCI_VERSION_1_1:
		return kSetCursorSci11(s, argc, argv);
	default:
		error("Unknown SetCursor type");
		return NULL_REG;
	}
}

reg_t kMoveCursor(EngineState *s, int argc, reg_t *argv) {
	g_sci->_gfxCursor->kernelSetPos(Common::Point(argv[0].toSint16(), argv[1].toSint16()));
	return s->r_acc;
}

reg_t kPicNotValid(EngineState *s, int argc, reg_t *argv) {
	int16 newPicNotValid = (argc > 0) ? argv[0].toUint16() : -1;

	return make_reg(0, g_sci->_gfxScreen->kernelPicNotValid(newPicNotValid));
}

static Common::Rect getGraphRect(reg_t *argv) {
	int16 x = argv[1].toSint16();
	int16 y = argv[0].toSint16();
	int16 x1 = argv[3].toSint16();
	int16 y1 = argv[2].toSint16();
	if (x > x1) SWAP(x, x1);
	if (y > y1) SWAP(y, y1);
	return Common::Rect(x, y, x1, y1);
}

static Common::Point getGraphPoint(reg_t *argv) {
	int16 x = argv[1].toSint16();
	int16 y = argv[0].toSint16();
	return Common::Point(x, y);
}

reg_t kGraph(EngineState *s, int argc, reg_t *argv) {
	if (!s)
		return make_reg(0, getSciVersion());
	error("not supposed to call this");
}

reg_t kGraphGetColorCount(EngineState *s, int argc, reg_t *argv) {
	return make_reg(0, g_sci->_gfxPalette16->getTotalColorCount());
}

reg_t kGraphDrawLine(EngineState *s, int argc, reg_t *argv) {
	int16 color = adjustGraphColor(argv[4].toSint16());
	int16 priority = (argc > 5) ? argv[5].toSint16() : -1;
	int16 control = (argc > 6) ? argv[6].toSint16() : -1;

	g_sci->_gfxPaint16->kernelGraphDrawLine(getGraphPoint(argv), getGraphPoint(argv + 2), color, priority, control);
	return s->r_acc;
}

reg_t kGraphSaveBox(EngineState *s, int argc, reg_t *argv) {
	Common::Rect rect = getGraphRect(argv);
	uint16 screenMask = argv[4].toUint16() & GFX_SCREEN_MASK_ALL;
	return g_sci->_gfxPaint16->kernelGraphSaveBox(rect, screenMask);
}

reg_t kGraphRestoreBox(EngineState *s, int argc, reg_t *argv) {
	// This may be called with a memoryhandle from SAVE_BOX or SAVE_UPSCALEDHIRES_BOX
	g_sci->_gfxPaint16->kernelGraphRestoreBox(argv[0]);
	return s->r_acc;
}

reg_t kGraphFillBoxBackground(EngineState *s, int argc, reg_t *argv) {
	Common::Rect rect = getGraphRect(argv);
	g_sci->_gfxPaint16->kernelGraphFillBoxBackground(rect);
	return s->r_acc;
}

reg_t kGraphFillBoxForeground(EngineState *s, int argc, reg_t *argv) {
	Common::Rect rect = getGraphRect(argv);
	g_sci->_gfxPaint16->kernelGraphFillBoxForeground(rect);
	return s->r_acc;
}

reg_t kGraphFillBoxAny(EngineState *s, int argc, reg_t *argv) {
	Common::Rect rect = getGraphRect(argv);
	int16 colorMask = argv[4].toUint16();
	int16 color = adjustGraphColor(argv[5].toSint16());
	int16 priority = argv[6].toSint16(); // yes, we may read from stack sometimes here
	int16 control = argv[7].toSint16(); // sierra did the same

	g_sci->_gfxPaint16->kernelGraphFillBox(rect, colorMask, color, priority, control);
	return s->r_acc;
}

reg_t kGraphUpdateBox(EngineState *s, int argc, reg_t *argv) {
	Common::Rect rect = getGraphRect(argv);
	// argv[4] is the map (1 for visual, etc.)
	// argc == 6 on upscaled hires
	bool hiresMode = (argc > 5) ? true : false;
	g_sci->_gfxPaint16->kernelGraphUpdateBox(rect, hiresMode);
	return s->r_acc;
}

reg_t kGraphRedrawBox(EngineState *s, int argc, reg_t *argv) {
	Common::Rect rect = getGraphRect(argv);
	g_sci->_gfxPaint16->kernelGraphRedrawBox(rect);
	return s->r_acc;
}

// Seems to be only implemented for SCI0/SCI01 games
reg_t kGraphAdjustPriority(EngineState *s, int argc, reg_t *argv) {
	g_sci->_gfxPorts->kernelGraphAdjustPriority(argv[0].toUint16(), argv[1].toUint16());
	return s->r_acc;
}

reg_t kGraphSaveUpscaledHiresBox(EngineState *s, int argc, reg_t *argv) {
	Common::Rect rect = getGraphRect(argv);
	return g_sci->_gfxPaint16->kernelGraphSaveUpscaledHiresBox(rect);
}

reg_t kTextSize(EngineState *s, int argc, reg_t *argv) {
	reg_t *dest = s->_segMan->derefRegPtr(argv[0], 4);
	Common::String text = s->_segMan->getString(argv[1]);
	int font = argv[2].toUint16();
	int maxWidth = (argc > 3) ? argv[3].toUint16() : 0;

	if (!dest) {
		debugC(kDebugLevelStrings, "GetTextSize: Empty destination");
		return s->r_acc;
	}

	Common::String separatorString;
	const char *separator = nullptr;
	if ((argc > 4) && (argv[4].getSegment())) {
		separatorString = s->_segMan->getString(argv[4]);
		separator = separatorString.c_str();
	}

	dest[0] = NULL_REG;
	dest[1] = NULL_REG;

	if (text.empty()) { // Empty text
		dest[2] = NULL_REG;
		dest[3] = NULL_REG;
		debugC(kDebugLevelStrings, "GetTextSize: Empty string");
		return s->r_acc;
	}

	uint16 languageSplitter = 0;
	Common::String splitText = g_sci->strSplitLanguage(text.c_str(), &languageSplitter, separator);

	int16 textWidth;
	int16 textHeight;
	const bool useMacFonts = g_sci->hasMacFonts() && (argc < 6);
	if (!useMacFonts) {
		g_sci->_gfxText16->kernelTextSize(splitText.c_str(), languageSplitter, font, maxWidth, &textWidth, &textHeight);
	} else {
		// Mac games with native fonts always use them for sizing unless a sixth
		// parameter is passed to indicate that SCI font sizing should be used.
		// Only LSL5 is known to pass this parameter in Dialog:setSize.
		g_sci->_gfxText16->macTextSize(splitText, font, g_sci->_gfxText16->GetFontId(), maxWidth, &textWidth, &textHeight);
	}

	debugC(kDebugLevelStrings, "GetTextSize '%s' -> %dx%d", text.c_str(), textWidth, textHeight);
	dest[2] = make_reg(0, textHeight);
	dest[3] = make_reg(0, textWidth);

	return s->r_acc;
}

// kWait is a throttling function that sleeps up to the requested
// number of ticks, or possibly not at all. The sleep duration
// is based on the time since kWait was last called.
reg_t kWait(EngineState *s, int argc, reg_t *argv) {
	uint16 ticks = argv[0].toUint16();

	const uint16 delta = s->wait(ticks);

	if (g_sci->_guestAdditions->kWaitHook()) {
		return NULL_REG;
	}

	s->_paletteSetIntensityCounter = 0;
	return make_reg(0, delta);
}

// kScummVMSleep is our own custom kernel function that sleeps for
// the number of ticks requested. We use this in script patches
// to replace spin loops so that the application remains responsive
// and doesn't just block the thread without updating the screen or
// processing input events.
reg_t kScummVMSleep(EngineState *s, int argc, reg_t *argv) {
	uint16 ticks = argv[0].toUint16();
	s->sleep(ticks);
	return s->r_acc;
}

reg_t kCoordPri(EngineState *s, int argc, reg_t *argv) {
	int16 y = argv[0].toSint16();

	if ((argc < 2) || (y != 1)) {
		return make_reg(0, g_sci->_gfxPorts->kernelCoordinateToPriority(y));
	} else {
		int16 priority = argv[1].toSint16();
		return make_reg(0, g_sci->_gfxPorts->kernelPriorityToCoordinate(priority));
	}
}

reg_t kPriCoord(EngineState *s, int argc, reg_t *argv) {
	int16 priority = argv[0].toSint16();

	return make_reg(0, g_sci->_gfxPorts->kernelPriorityToCoordinate(priority));
}

reg_t kDirLoop(EngineState *s, int argc, reg_t *argv) {
	kDirLoopWorker(argv[0], argv[1].toUint16(), s, argc, argv);

	return s->r_acc;
}

reg_t kCanBeHere(EngineState *s, int argc, reg_t *argv) {
	reg_t curObject = argv[0];
	reg_t listReference = (argc > 1) ? argv[1] : NULL_REG;

	reg_t canBeHere = g_sci->_gfxCompare->kernelCanBeHere(curObject, listReference);
	return make_reg(0, canBeHere.isNull() ? 1 : 0);
}

reg_t kCantBeHere(EngineState *s, int argc, reg_t *argv) {
	reg_t curObject = argv[0];
	reg_t listReference = (argc > 1) ? argv[1] : NULL_REG;

#ifdef ENABLE_SCI32
	if (getSciVersion() >= SCI_VERSION_2) {
		return g_sci->_gfxCompare->kernelCantBeHere32(curObject, listReference);
	} else {
#endif
		return g_sci->_gfxCompare->kernelCanBeHere(curObject, listReference);
#ifdef ENABLE_SCI32
	}
#endif
}

reg_t kIsItSkip(EngineState *s, int argc, reg_t *argv) {
	GuiResourceId viewId = argv[0].toSint16();
	int16 loopNo = argv[1].toSint16();
	int16 celNo = argv[2].toSint16();
	Common::Point position(argv[4].toUint16(), argv[3].toUint16());

	bool result = g_sci->_gfxCompare->kernelIsItSkip(viewId, loopNo, celNo, position);
	return make_reg(0, result);
}

reg_t kCelHigh(EngineState *s, int argc, reg_t *argv) {
	GuiResourceId viewId = argv[0].toSint16();
	if (viewId == -1)	// Happens in SCI32
		return NULL_REG;
	int16 loopNo = argv[1].toSint16();
	int16 celNo = (argc >= 3) ? argv[2].toSint16() : 0;
	int16 celHeight;

	celHeight = g_sci->_gfxCache->kernelViewGetCelHeight(viewId, loopNo, celNo);

	return make_reg(0, celHeight);
}

reg_t kCelWide(EngineState *s, int argc, reg_t *argv) {
	GuiResourceId viewId = argv[0].toSint16();
	if (viewId == -1)	// Happens in SCI32
		return NULL_REG;
	int16 loopNo = argv[1].toSint16();
	int16 celNo = (argc >= 3) ? argv[2].toSint16() : 0;
	int16 celWidth;

	celWidth = g_sci->_gfxCache->kernelViewGetCelWidth(viewId, loopNo, celNo);

	return make_reg(0, celWidth);
}

reg_t kNumLoops(EngineState *s, int argc, reg_t *argv) {
	reg_t object = argv[0];
	GuiResourceId viewId = readSelectorValue(s->_segMan, object, SELECTOR(view));
	int16 loopCount;

#ifdef ENABLE_SCI32
	if (getSciVersion() >= SCI_VERSION_2) {
		loopCount = CelObjView::getNumLoops(viewId);
	} else
#endif
		loopCount = g_sci->_gfxCache->kernelViewGetLoopCount(viewId);

	debugC(9, kDebugLevelGraphics, "NumLoops(view.%d) = %d", viewId, loopCount);

	return make_reg(0, loopCount);
}

reg_t kNumCels(EngineState *s, int argc, reg_t *argv) {
	reg_t object = argv[0];
	GuiResourceId viewId = readSelectorValue(s->_segMan, object, SELECTOR(view));
	int16 loopNo = readSelectorValue(s->_segMan, object, SELECTOR(loop));
	int16 celCount;

#ifdef ENABLE_SCI32
	if (getSciVersion() >= SCI_VERSION_2) {
		celCount = CelObjView::getNumCels(viewId, loopNo);
	} else
#endif
		celCount = g_sci->_gfxCache->kernelViewGetCelCount(viewId, loopNo);

	debugC(9, kDebugLevelGraphics, "NumCels(view.%d, %d) = %d", viewId, loopNo, celCount);

	return make_reg(0, celCount);
}

reg_t kOnControl(EngineState *s, int argc, reg_t *argv) {
	Common::Rect rect;
	byte screenMask;
	int argBase = 0;

	if ((argc == 2) || (argc == 4)) {
		screenMask = GFX_SCREEN_MASK_CONTROL;
	} else {
		screenMask = argv[0].toUint16();
		argBase = 1;
	}
	rect.left = argv[argBase].toSint16();
	rect.top = argv[argBase + 1].toSint16();
	if (argc > 3) {
		rect.right = argv[argBase + 2].toSint16();
		rect.bottom = argv[argBase + 3].toSint16();
	} else {
		rect.right = rect.left + 1;
		rect.bottom = rect.top + 1;
	}
	uint16 result = g_sci->_gfxCompare->kernelOnControl(screenMask, rect);
	return make_reg(0, result);
}

#define K_DRAWPIC_FLAGS_MIRRORED			(1 << 14)
#define K_DRAWPIC_FLAGS_ANIMATIONBLACKOUT	(1 << 15)

reg_t kDrawPic(EngineState *s, int argc, reg_t *argv) {
	GuiResourceId pictureId = argv[0].toUint16();
	uint16 flags = 0;
	int16 animationNr = -1;
	bool animationBlackoutFlag = false;
	bool mirroredFlag = false;
	bool addToFlag = false;
	int16 EGApaletteNo = 0; // default needs to be 0

	if (argc >= 2) {
		flags = argv[1].toUint16();
		if (flags & K_DRAWPIC_FLAGS_ANIMATIONBLACKOUT)
			animationBlackoutFlag = true;
		animationNr = flags & 0xFF;
		// Mac interpreters ignored the mirrored flag and didn't mirror pics.
		//  KQ6 PC room 390 drew pic 390 mirrored so Mac added pic 395, which
		//  is a mirror of 390, but the script continued to pass this flag.
		if (g_sci->getPlatform() != Common::kPlatformMacintosh) {
			if (flags & K_DRAWPIC_FLAGS_MIRRORED)
				mirroredFlag = true;
		}
	}
	if (argc >= 3) {
		if (!argv[2].isNull())
			addToFlag = true;
		if (!g_sci->_features->usesOldGfxFunctions())
			addToFlag = !addToFlag;
	}
	if (argc >= 4)
		EGApaletteNo = argv[3].toUint16();

	g_sci->_gfxPaint16->kernelDrawPicture(pictureId, animationNr, animationBlackoutFlag, mirroredFlag, addToFlag, EGApaletteNo);

	return s->r_acc;
}

reg_t kBaseSetter(EngineState *s, int argc, reg_t *argv) {
	reg_t object = argv[0];

	g_sci->_gfxCompare->kernelBaseSetter(object);
	return s->r_acc;
}

reg_t kSetNowSeen(EngineState *s, int argc, reg_t *argv) {
	g_sci->_gfxCompare->kernelSetNowSeen(argv[0]);
	return s->r_acc;
}

reg_t kPalette(EngineState *s, int argc, reg_t *argv) {
	if (!s)
		return make_reg(0, getSciVersion());
	error("not supposed to call this");
}

reg_t kPaletteSetFromResource(EngineState *s, int argc, reg_t *argv) {
	GuiResourceId resourceId = argv[0].toUint16();
	bool force = false;
	if (argc == 2)
		force = argv[1].toUint16() == 2 ? true : false;

	// Non-VGA games don't use palette resources.
	// This has been changed to 64 colors because Longbow Amiga does have
	// one palette (palette 999).
	if (g_sci->_gfxPalette16->getTotalColorCount() < 64)
		return s->r_acc;

	g_sci->_gfxPalette16->kernelSetFromResource(resourceId, force);
	return s->r_acc;
}

reg_t kPaletteSetFlag(EngineState *s, int argc, reg_t *argv) {
	uint16 fromColor = CLIP<uint16>(argv[0].toUint16(), 1, 255);
	uint16 toColor = CLIP<uint16>(argv[1].toUint16(), 1, 255);
	uint16 flags = argv[2].toUint16();
	g_sci->_gfxPalette16->kernelSetFlag(fromColor, toColor, flags);
	return s->r_acc;
}

reg_t kPaletteUnsetFlag(EngineState *s, int argc, reg_t *argv) {
	uint16 fromColor = CLIP<uint16>(argv[0].toUint16(), 1, 255);
	uint16 toColor = CLIP<uint16>(argv[1].toUint16(), 1, 255);
	uint16 flags = argv[2].toUint16();
	g_sci->_gfxPalette16->kernelUnsetFlag(fromColor, toColor, flags);
	return s->r_acc;
}

reg_t kPaletteSetIntensity(EngineState *s, int argc, reg_t *argv) {
	uint16 fromColor = CLIP<uint16>(argv[0].toUint16(), 1, 255);
	uint16 toColor = CLIP<uint16>(argv[1].toUint16(), 1, 255);
	uint16 intensity = argv[2].toUint16();
	bool setPalette = (argc < 4) ? true : (argv[3].isNull()) ? true : false;

	// Palette intensity in non-VGA SCI1 games has been removed
	if (g_sci->_gfxPalette16->getTotalColorCount() < 256)
		return s->r_acc;

	if (setPalette) {
		// Detect if we're being called from an unthrottled script loop.
		// Throttled loops that call kWait on each iteration are okay.
		if (s->_paletteSetIntensityCounter > 0) {
			// Call speed throttler, otherwise the palette fade from this
			// unthrottled script loop won't have any visible effect.
			// Examples: KQ6 intro text/credits and SQ4CD intro credits
			s->speedThrottler(30);
		}
		s->_paletteSetIntensityCounter++;

		// Enable normal throttling in case this is being called from a script that
		// doesn't animate anything with kAnimate, such as the LB2 title screen.
		s->_throttleTrigger = true;
	}

	g_sci->_gfxPalette16->kernelSetIntensity(fromColor, toColor, intensity, setPalette);
	return s->r_acc;
}

reg_t kPaletteFindColor(EngineState *s, int argc, reg_t *argv) {
	uint16 r = argv[0].toUint16();
	uint16 g = argv[1].toUint16();
	uint16 b = argv[2].toUint16();
	return make_reg(0, g_sci->_gfxPalette16->kernelFindColor(r, g, b));
}

reg_t kPaletteAnimate(EngineState *s, int argc, reg_t *argv) {
	int16 argNr;
	bool paletteChanged = false;

	// Palette animation in non-VGA SCI1 games has been removed
	if (g_sci->_gfxPalette16->getTotalColorCount() == 256) {
		for (argNr = 0; argNr < argc; argNr += 3) {
			uint16 fromColor = argv[argNr].toUint16();
			uint16 toColor = argv[argNr + 1].toUint16();
			int16 speed = argv[argNr + 2].toSint16();
			if (g_sci->_gfxPalette16->kernelAnimate(fromColor, toColor, speed))
				paletteChanged = true;
		}
		if (paletteChanged)
			g_sci->_gfxPalette16->kernelAnimateSet();
	}

	// WORKAROUNDS: kPaletteAnimate produces different results in ScummVM than
	// the original when multiple calls occur in the same game cycle.
	// SSCI updated the screen immediately so each call took a noticeable amount
	// of time and the results of each call were visible.
	// We generally update the screen on each game cycle; that makes all of the
	// palette changes appear at once. No extra delay is produced since updating
	// the palette data by itself takes an insignificant amount of time.
	// Most scripts that call kPaletteAnimate only do so once per game cycle, so
	// they are unaffected. Most that call it multiple times achieve practically
	// the same effect in ScummVM. (Longbow title screen, EcoQuest ocean rooms,
	// QFG1VGA room 10) But for scripts or effects that depend on the delay,
	// or seeing each individual update, we currently work around them.

	// WORKAROUND: The game scripts in SQ4 floppy count the number of elapsed
	// cycles in the intro from the number of successive kAnimate calls during
	// the palette cycling effect, while showing the SQ4 logo. This worked in
	// older computers because each animate call took awhile to complete.
	// Normally, such scripts are handled automatically by our speed throttler,
	// however in this case there are no calls to kGameIsRestarting (where the
	// speed throttler gets called) between the different palette animation calls.
	// Thus, we add a small delay between each animate call to make the whole
	// palette animation effect slower and visible, and not have the logo screen
	// get skipped because the scripts don't wait between animation steps. This
	// workaround is applied to non-VGA versions as well because even though they
	// don't use palette animation they still call this function and use it for
	// timing. Fixes bugs #6057, #6193.
	// The original workaround was for the intro SQ4 logo (room#1).
	// This problem also happens in the time pod (room#531).
	// This problem also happens in the ending cutscene time rip (room#21).
	// This workaround affects astro chicken's (room#290) and is also called once
	// right after a gameover (room#376)
	if (g_sci->getGameId() == GID_SQ4 && !g_sci->isCD())
		g_sci->sleep(10);

	// WORKAROUND: PQ1 and PQ3 title screens call kPaletteAnimate eight times
	// on each game cycle to animate police lights. The effect relies on every
	// palette change being drawn to the screen instead of just the last one.
	// We fix this by updating the screen on every call. Normally we would want
	// to process events to keep the cursor smooth during these lengthy game
	// cycles, but it doesn't matter here because the cursor is hidden.
	// We call OSystem::updateScreen() directly to avoid the SCI throttler that
	// discards multiple updates within 1/60th of a second, as that can lose
	// some of the animation frames. This is only applied to the VGA version.
	if ((g_sci->getGameId() == GID_PQ1 && s->currentRoomNumber() == 1) ||
		(g_sci->getGameId() == GID_PQ3 && s->currentRoomNumber() == 2)) {
		// PQ1 also cycles the Sierra logo in its room 1, so limit the
		// workaround to just the police lights.
		uint16 fromColor = argv[0].toUint16();
		if (fromColor >= 208 && paletteChanged) {
			g_system->updateScreen();
		}
	}

	return s->r_acc;
}

reg_t kPaletteSave(EngineState *s, int argc, reg_t *argv) {
	return g_sci->_gfxPalette16->kernelSave();
}

reg_t kPaletteRestore(EngineState *s, int argc, reg_t *argv) {
	g_sci->_gfxPalette16->kernelRestore(argv[0]);
	return argv[0];
}

reg_t kPalVary(EngineState *s, int argc, reg_t *argv) {
	if (!s)
		return make_reg(0, getSciVersion());
	error("not supposed to call this");
}

reg_t kPalVaryInit(EngineState *s, int argc, reg_t *argv) {
	GuiResourceId paletteId = argv[0].toUint16();
	uint16 ticks = argv[1].toUint16();
	uint16 stepStop = argc >= 3 ? argv[2].toUint16() : 64;
	uint16 direction = argc >= 4 ? argv[3].toUint16() : 1;
	if (g_sci->_gfxPalette16->kernelPalVaryInit(paletteId, ticks, stepStop, direction))
		return SIGNAL_REG;
	return NULL_REG;
}

reg_t kPalVaryReverse(EngineState *s, int argc, reg_t *argv) {
	int16 ticks = argc >= 1 ? argv[0].toUint16() : -1;
	int16 stepStop = argc >= 2 ? argv[1].toUint16() : 0;
	int16 direction = argc >= 3 ? argv[2].toSint16() : -1;

	return make_reg(0, g_sci->_gfxPalette16->kernelPalVaryReverse(ticks, stepStop, direction));
}

reg_t kPalVaryGetCurrentStep(EngineState *s, int argc, reg_t *argv) {
	return make_reg(0, g_sci->_gfxPalette16->kernelPalVaryGetCurrentStep());
}

reg_t kPalVaryDeinit(EngineState *s, int argc, reg_t *argv) {
	g_sci->_gfxPalette16->kernelPalVaryDeinit();
	return NULL_REG;
}

reg_t kPalVaryChangeTarget(EngineState *s, int argc, reg_t *argv) {
	GuiResourceId paletteId = argv[0].toUint16();
	int16 currentStep = g_sci->_gfxPalette16->kernelPalVaryChangeTarget(paletteId);
	return make_reg(0, currentStep);
}

reg_t kPalVaryChangeTicks(EngineState *s, int argc, reg_t *argv) {
	uint16 ticks = argv[0].toUint16();
	g_sci->_gfxPalette16->kernelPalVaryChangeTicks(ticks);
	return NULL_REG;
}

reg_t kPalVaryPauseResume(EngineState *s, int argc, reg_t *argv) {
	bool pauseState = !argv[0].isNull();
	g_sci->_gfxPalette16->kernelPalVaryPause(pauseState);
	return NULL_REG;
}

reg_t kAssertPalette(EngineState *s, int argc, reg_t *argv) {
	GuiResourceId paletteId = argv[0].toUint16();

	g_sci->_gfxPalette16->kernelAssertPalette(paletteId);
	return s->r_acc;
}

// Used to show hires character portraits in the Windows CD version of KQ6
reg_t kPortrait(EngineState *s, int argc, reg_t *argv) {
	uint16 operation = argv[0].toUint16();

	switch (operation) {
	case 0: { // load
		if (argc == 2) {
			Common::String resourceName = s->_segMan->getString(argv[1]);
			s->r_acc = g_sci->_gfxPaint16->kernelPortraitLoad(resourceName);
		} else {
			error("kPortrait(loadResource) called with unsupported argc %d", argc);
		}
		break;
	}
	case 1: { // show
		if (argc == 10) {
			Common::String resourceName = s->_segMan->getString(argv[1]);
			Common::Point position = Common::Point(argv[2].toUint16(), argv[3].toUint16());
			uint resourceNum = argv[4].toUint16();
			uint noun = argv[5].toUint16() & 0xff;
			uint verb = argv[6].toUint16() & 0xff;
			uint cond = argv[7].toUint16() & 0xff;
			uint seq = argv[8].toUint16() & 0xff;
			// argv[9] is usually 0??!!

			g_sci->_gfxPaint16->kernelPortraitShow(resourceName, position, resourceNum, noun, verb, cond, seq);
			return SIGNAL_REG;
		} else {
			error("kPortrait(show) called with unsupported argc %d", argc);
		}
		break;
	}
	case 2: { // unload
		if (argc == 2) {
			uint16 portraitId = argv[1].toUint16();
			g_sci->_gfxPaint16->kernelPortraitUnload(portraitId);
		} else {
			error("kPortrait(unload) called with unsupported argc %d", argc);
		}
		break;
	}
	default:
		error("kPortrait(%d), not implemented (argc = %d)", operation, argc);
	}

	return s->r_acc;
}

// Original top-left must stay on kControl rects, we adjust accordingly because
// sierra sci actually wont draw rects that are upside down (example: jones,
// when challenging jones - one button is a duplicate and also has lower-right
// which is 0, 0)
Common::Rect kControlCreateRect(int16 x, int16 y, int16 x1, int16 y1) {
	if (x > x1) x1 = x;
	if (y > y1) y1 = y;
	return Common::Rect(x, y, x1, y1);
}

void _k_GenericDrawControl(EngineState *s, reg_t controlObject, bool hilite) {
	int16 type = readSelectorValue(s->_segMan, controlObject, SELECTOR(type));
	int16 style = readSelectorValue(s->_segMan, controlObject, SELECTOR(state));
	int16 x = readSelectorValue(s->_segMan, controlObject, SELECTOR(nsLeft));
	int16 y = readSelectorValue(s->_segMan, controlObject, SELECTOR(nsTop));
	GuiResourceId fontId = readSelectorValue(s->_segMan, controlObject, SELECTOR(font));
	reg_t textReference = readSelector(s->_segMan, controlObject, SELECTOR(text));
	Common::String text;
	Common::Rect rect;
	TextAlignment alignment;
	int16 mode, maxChars, cursorPos, upperPos, listCount, i;
	uint16 upperOffset, cursorOffset;
	GuiResourceId viewId;
	int16 loopNo;
	int16 celNo;
	int16 priority;
	reg_t listSeeker;
	Common::String *listStrings = nullptr;
	bool isAlias = false;

	rect = kControlCreateRect(x, y,
				readSelectorValue(s->_segMan, controlObject, SELECTOR(nsRight)),
				readSelectorValue(s->_segMan, controlObject, SELECTOR(nsBottom)));

	if (!textReference.isNull())
		text = s->_segMan->getString(textReference);

	uint16 languageSplitter = 0;
	Common::String splitText;

	switch (type) {
	case SCI_CONTROLS_TYPE_BUTTON:
	case SCI_CONTROLS_TYPE_TEXTEDIT:
		splitText = g_sci->strSplitLanguage(text.c_str(), &languageSplitter, nullptr);
		break;
	case SCI_CONTROLS_TYPE_TEXT:
		splitText = g_sci->strSplitLanguage(text.c_str(), &languageSplitter);
		break;
	default:
		break;
	}

	switch (type) {
	case SCI_CONTROLS_TYPE_BUTTON:
		debugC(kDebugLevelGraphics, "drawing button %04x:%04x to %d,%d", PRINT_REG(controlObject), x, y);
		g_sci->_gfxControls16->kernelDrawButton(rect, controlObject, splitText.c_str(), languageSplitter, fontId, style, hilite);
		return;

	case SCI_CONTROLS_TYPE_TEXT:
		alignment = readSelectorValue(s->_segMan, controlObject, SELECTOR(mode));
		debugC(kDebugLevelGraphics, "drawing text %04x:%04x ('%s') to %d,%d, mode=%d", PRINT_REG(controlObject), text.c_str(), x, y, alignment);
		g_sci->_gfxControls16->kernelDrawText(rect, controlObject, splitText.c_str(), languageSplitter, fontId, alignment, style, hilite);
		s->r_acc = g_sci->_gfxText16->allocAndFillReferenceRectArray();
		return;

	case SCI_CONTROLS_TYPE_TEXTEDIT:
		mode = readSelectorValue(s->_segMan, controlObject, SELECTOR(mode));
		maxChars = readSelectorValue(s->_segMan, controlObject, SELECTOR(max));
		cursorPos = readSelectorValue(s->_segMan, controlObject, SELECTOR(cursor));
		if (cursorPos > (int)text.size()) {
			// if cursor is outside of text, adjust accordingly
			cursorPos = text.size();
			writeSelectorValue(s->_segMan, controlObject, SELECTOR(cursor), cursorPos);
		}
		debugC(kDebugLevelGraphics, "drawing edit control %04x:%04x (text %04x:%04x, '%s') to %d,%d", PRINT_REG(controlObject), PRINT_REG(textReference), text.c_str(), x, y);
		g_sci->_gfxControls16->kernelDrawTextEdit(rect, controlObject, splitText.c_str(), languageSplitter, fontId, mode, style, cursorPos, maxChars, hilite);
		return;

	case SCI_CONTROLS_TYPE_ICON:
		viewId = readSelectorValue(s->_segMan, controlObject, SELECTOR(view));
		{
			int l = readSelectorValue(s->_segMan, controlObject, SELECTOR(loop));
			loopNo = (l & 0x80) ? l - 256 : l;
			int c = readSelectorValue(s->_segMan, controlObject, SELECTOR(cel));
			celNo = (c & 0x80) ? c - 256 : c;
			// Check if the control object specifies a priority selector (like in Jones)
			if (lookupSelector(s->_segMan, controlObject, SELECTOR(priority), nullptr, nullptr) == kSelectorVariable)
				priority = readSelectorValue(s->_segMan, controlObject, SELECTOR(priority));
			else
				priority = -1;
		}
		debugC(kDebugLevelGraphics, "drawing icon control %04x:%04x to %d,%d", PRINT_REG(controlObject), x, y - 1);
		g_sci->_gfxControls16->kernelDrawIcon(rect, controlObject, viewId, loopNo, celNo, priority, style, hilite);
		return;

	case SCI_CONTROLS_TYPE_LIST:
	case SCI_CONTROLS_TYPE_LIST_ALIAS:
		if (type == SCI_CONTROLS_TYPE_LIST_ALIAS)
			isAlias = true;

		maxChars = readSelectorValue(s->_segMan, controlObject, SELECTOR(x)); // max chars per entry
		cursorOffset = readSelectorValue(s->_segMan, controlObject, SELECTOR(cursor));
		if (SELECTOR(topString) != -1) {
			// Games from early SCI1 onwards use topString
			upperOffset = readSelectorValue(s->_segMan, controlObject, SELECTOR(topString));
		} else {
			// Earlier games use lsTop or brTop
			if (lookupSelector(s->_segMan, controlObject, SELECTOR(brTop), nullptr, nullptr) == kSelectorVariable)
				upperOffset = readSelectorValue(s->_segMan, controlObject, SELECTOR(brTop));
			else
				upperOffset = readSelectorValue(s->_segMan, controlObject, SELECTOR(lsTop));
		}

		// Count string entries in NULL terminated string list
		listCount = 0; listSeeker = textReference;
		while (s->_segMan->strlen(listSeeker) > 0) {
			listCount++;
			listSeeker.incOffset(maxChars);
		}

		// TODO: This is rather convoluted... It would be a lot cleaner
		// if sciw_new_list_control would take a list of Common::String
		cursorPos = 0; upperPos = 0;
		if (listCount) {
			// We create a pointer-list to the different strings, we also find out whats upper and cursor position
			listSeeker = textReference;
			listStrings = new Common::String[listCount];
			for (i = 0; i < listCount; i++) {
				listStrings[i] = s->_segMan->getString(listSeeker);
				if (listSeeker.getOffset() == upperOffset)
					upperPos = i;
				if (listSeeker.getOffset() == cursorOffset)
					cursorPos = i;
				listSeeker.incOffset(maxChars);
			}
		}

		debugC(kDebugLevelGraphics, "drawing list control %04x:%04x to %d,%d", PRINT_REG(controlObject), x, y);
		g_sci->_gfxControls16->kernelDrawList(rect, controlObject, maxChars, listCount, listStrings, fontId, style, upperPos, cursorPos, isAlias, hilite);
		delete[] listStrings;
		return;

	case SCI_CONTROLS_TYPE_DUMMY:
		// Actually this here does nothing at all, its required by at least QfG1/EGA that we accept this type
		return;

	default:
		error("unsupported control type %d", type);
	}
}

reg_t kDrawControl(EngineState *s, int argc, reg_t *argv) {
	reg_t controlObject = argv[0];
	Common::String objName = s->_segMan->getObjectName(controlObject);

	// Most of the time, we won't return anything to the caller
	//  but |r| textcodes will trigger creation of rects in memory and will then set s->r_acc
	s->r_acc = NULL_REG;

	// Disable the "Change Directory" button, as we don't allow the game engine to
	// change the directory where saved games are placed
	// "changeDirItem" is used in the import windows of QFG2&3
	if ((objName == "changeDirI") || (objName == "changeDirItem")) {
		int state = readSelectorValue(s->_segMan, controlObject, SELECTOR(state));
		writeSelectorValue(s->_segMan, controlObject, SELECTOR(state), (state | SCI_CONTROLS_STYLE_DISABLED) & ~SCI_CONTROLS_STYLE_ENABLED);
	}
	if (objName == "DEdit") {
		reg_t textReference = readSelector(s->_segMan, controlObject, SELECTOR(text));
		if (!textReference.isNull()) {
			Common::String text = s->_segMan->getString(textReference);
			if ((text == "a:hq1_hero.sav") || (text == "a:glory1.sav") || (text == "a:glory2.sav") || (text == "a:glory3.sav") || (text == "a:gloire3.sauv")) {
				// Remove "a:" from hero quest / quest for glory export default filenames
				// The french version of Quest For Glory 3 uses "gloire3.sauv". It seems a translator translated the filename.
				text.deleteChar(0);
				text.deleteChar(0);
				s->_segMan->strcpy_(textReference, text.c_str());
			}
		}
	}
	if (objName == "savedHeros") {
		// Import of QfG character files dialog is shown.
		// Display additional popup information before letting user use it.
		// For the SCI32 version of this, check kernelAddPlane().
		reg_t changeDirButton = s->_segMan->findObjectByName("changeDirItem");
		if (!changeDirButton.isNull()) {
			// check if checkDirButton is still enabled, in that case we are called the first time during that room
			if (!(readSelectorValue(s->_segMan, changeDirButton, SELECTOR(state)) & SCI_CONTROLS_STYLE_DISABLED)) {
				g_sci->showQfgImportMessageBox();
			}
		}

		// For the SCI32 version of this, check kListAt().
		s->_chosenQfGImportItem = readSelectorValue(s->_segMan, controlObject, SELECTOR(mark));
	}

	_k_GenericDrawControl(s, controlObject, false);
	return s->r_acc;
}

reg_t kHiliteControl(EngineState *s, int argc, reg_t *argv) {
	reg_t controlObject = argv[0];

	_k_GenericDrawControl(s, controlObject, true);
	return s->r_acc;
}

reg_t kEditControl(EngineState *s, int argc, reg_t *argv) {
	reg_t controlObject = argv[0];
	reg_t eventObject = argv[1];

	if (!controlObject.isNull()) {
		int16 controlType = readSelectorValue(s->_segMan, controlObject, SELECTOR(type));

		switch (controlType) {
		case SCI_CONTROLS_TYPE_TEXTEDIT:
			// Only process textedit controls in here
			g_sci->_gfxControls16->kernelTexteditChange(controlObject, eventObject);
			break;
		default:
			break;
		}
	}
	return s->r_acc;
}

reg_t kAddToPic(EngineState *s, int argc, reg_t *argv) {
	GuiResourceId viewId;
	int16 loopNo;
	int16 celNo;
	int16 leftPos, topPos, priority, control;

	switch (argc) {
	// Is this ever really gets called with 0 parameters, we need to set _picNotValid!!
	//case 0:
	//	break;
	case 1:
		if (argv[0].isNull())
			return s->r_acc;
		g_sci->_gfxAnimate->kernelAddToPicList(argv[0], argc, argv);
		break;
	case 7:
		viewId = argv[0].toUint16();
		loopNo = argv[1].toSint16();
		celNo = argv[2].toSint16();
		leftPos = argv[3].toSint16();
		topPos = argv[4].toSint16();
		priority = argv[5].toSint16();
		control = argv[6].toSint16();
		g_sci->_gfxAnimate->kernelAddToPicView(viewId, loopNo, celNo, leftPos, topPos, priority, control);
		break;
	default:
		error("kAddToPic with unsupported parameter count %d", argc);
	}
	return s->r_acc;
}

reg_t kGetPort(EngineState *s, int argc, reg_t *argv) {
	return g_sci->_gfxPorts->kernelGetActive();
}

reg_t kSetPort(EngineState *s, int argc, reg_t *argv) {
	uint16 portId;
	Common::Rect picRect;
	int16 picTop, picLeft;
	bool initPriorityBandsFlag = false;

	switch (argc) {
	case 1:
		portId = argv[0].toSint16();
		g_sci->_gfxPorts->kernelSetActive(portId);
		break;

	case 7:
		initPriorityBandsFlag = true;
		// fall through
	case 6:
		picRect.top = argv[0].toSint16();
		picRect.left = argv[1].toSint16();
		picRect.bottom = argv[2].toSint16();
		picRect.right = argv[3].toSint16();
		picTop = argv[4].toSint16();
		picLeft = argv[5].toSint16();
		g_sci->_gfxPorts->kernelSetPicWindow(picRect, picTop, picLeft, initPriorityBandsFlag);
		break;

	default:
		error("SetPort was called with %d parameters", argc);
		break;
	}
	return s->r_acc;
}

reg_t kDrawCel(EngineState *s, int argc, reg_t *argv) {
	GuiResourceId viewId = argv[0].toSint16();
	int16 loopNo = argv[1].toSint16();
	int16 celNo = argv[2].toSint16();
	uint16 x = argv[3].toUint16();
	uint16 y = argv[4].toUint16();
	int16 priority = (argc > 5) ? argv[5].toSint16() : -1;
	uint16 paletteNo = (argc > 6) ? argv[6].toUint16() : 0;
	bool hiresMode = false;
	reg_t upscaledHiresHandle = NULL_REG;
	uint16 scaleX = 128;
	uint16 scaleY = 128;

	if (argc > 7) {
		// this is either kq6 hires or scaling
		if (paletteNo > 0) {
			// it's scaling
			scaleX = argv[6].toUint16();
			scaleY = argv[7].toUint16();
			paletteNo = 0;
		} else {
			// KQ6 hires
			hiresMode = true;
			upscaledHiresHandle = argv[7];
		}
	}

	g_sci->_gfxPaint16->kernelDrawCel(viewId, loopNo, celNo, x, y, priority, paletteNo, scaleX, scaleY, hiresMode, upscaledHiresHandle);

	return s->r_acc;
}

reg_t kDisposeWindow(EngineState *s, int argc, reg_t *argv) {
	int windowId = argv[0].toSint16();
	bool reanimate = false;
	if ((argc != 2) || (argv[1].isNull()))
		reanimate = true;

	g_sci->_gfxPorts->kernelDisposeWindow(windowId, reanimate);
	g_sci->_tts->stop();

	return s->r_acc;
}

reg_t kNewWindow(EngineState *s, int argc, reg_t *argv) {
	Common::Rect rect1 (argv[1].toSint16(), argv[0].toSint16(), argv[3].toSint16(), argv[2].toSint16());
	Common::Rect rect2;
	int argextra = argc >= 13 ? 4 : 0; // Triggers in PQ3 and SCI1.1 games, argc 13 for DOS argc 15 for mac
	int	style = argv[5 + argextra].toSint16();
	int	priority = (argc > 6 + argextra) ? argv[6 + argextra].toSint16() : -1;
	int colorPen = adjustGraphColor((argc > 7 + argextra) ? argv[7 + argextra].toSint16() : 0);
	int colorBack = adjustGraphColor((argc > 8 + argextra) ? argv[8 + argextra].toSint16() : 255);

	if (argc >= 13)
		rect2 = Common::Rect (argv[5].toSint16(), argv[4].toSint16(), argv[7].toSint16(), argv[6].toSint16());

	Common::String title;
	if (argv[4 + argextra].getSegment()) {
		title = s->_segMan->getString(argv[4 + argextra]);
		title = g_sci->strSplit(title.c_str(), nullptr);
	}

	return g_sci->_gfxPorts->kernelNewWindow(rect1, rect2, style, priority, colorPen, colorBack, title.c_str());
}

reg_t kAnimate(EngineState *s, int argc, reg_t *argv) {
	reg_t castListReference = (argc > 0) ? argv[0] : NULL_REG;
	bool cycle = (argc > 1) ? ((argv[1].toUint16()) ? true : false) : false;

	g_sci->_gfxAnimate->kernelAnimate(castListReference, cycle, argc, argv);

	// WORKAROUND: At the end of Ecoquest 1, during the credits, the game
	// doesn't call kGetEvent(), so no events are processed (e.g. window
	// focusing, window moving etc). We poll events for that scene, to
	// keep ScummVM responsive. Fixes ScummVM "freezing" during the credits,
	// bug #5494
	if (g_sci->getGameId() == GID_ECOQUEST && s->currentRoomNumber() == 680)
		g_sci->getEventManager()->getSciEvent(kSciEventPeek);

	return s->r_acc;
}

reg_t kShakeScreen(EngineState *s, int argc, reg_t *argv) {
	int16 shakeCount = (argc > 0) ? argv[0].toUint16() : 1;
	int16 directions = (argc > 1) ? argv[1].toUint16() : 1;

	g_sci->_gfxScreen->kernelShakeScreen(shakeCount, directions);
	return s->r_acc;
}

reg_t kDisplay(EngineState *s, int argc, reg_t *argv) {
	reg_t textp = argv[0];
	int index = (argc > 1) ? argv[1].toUint16() : 0;

	Common::String text;

	if (textp.getSegment()) {
		argc--; argv++;
		text = s->_segMan->getString(textp);
	} else {
		argc--; argc--; argv++; argv++;
		text = g_sci->getKernel()->lookupText(textp, index);
	}

	uint16 languageSplitter = 0;
	Common::String splitText = g_sci->strSplitLanguage(text.c_str(), &languageSplitter);

	return g_sci->_gfxPaint16->kernelDisplay(splitText.c_str(), languageSplitter, argc, argv);
}

reg_t kSetVideoMode(EngineState *s, int argc, reg_t *argv) {
	// This call is used for KQ6's intro. It has one parameter, which is 1 when
	// the intro begins, and 0 when it ends. It is suspected that this is
	// actually a flag to enable video planar memory access, as the video
	// decoder in KQ6 is specifically written for the planar memory model.
	// Planar memory mode access was used for VGA "Mode X" (320x240 resolution,
	// although the intro in KQ6 is 320x200).
	// Refer to http://en.wikipedia.org/wiki/Mode_X

	//warning("STUB: SetVideoMode %d", argv[0].toUint16());
	return s->r_acc;
}

// New calls for SCI11. Using those is only needed when using text-codes so that
// one is able to change font and/or color multiple times during kDisplay and
// kDrawControl
reg_t kTextFonts(EngineState *s, int argc, reg_t *argv) {
	g_sci->_gfxText16->kernelTextFonts(argc, argv);
	return s->r_acc;
}

reg_t kTextColors(EngineState *s, int argc, reg_t *argv) {
	g_sci->_gfxText16->kernelTextColors(argc, argv);
	return s->r_acc;
}

/**
 * Debug command, used by the SCI builtin debugger
 */
reg_t kShow(EngineState *s, int argc, reg_t *argv) {
	uint16 map = argv[0].toUint16();

	switch (map) {
	case 1:	// Visual, substituted by display for us
		g_sci->_gfxScreen->debugShowMap(3);
		break;
	case 2:	// Priority
		g_sci->_gfxScreen->debugShowMap(1);
		break;
	case 3:	// Control
	case 4:	// Control
		g_sci->_gfxScreen->debugShowMap(2);
		break;
	default:
		warning("Map %d is not available", map);
	}

	return s->r_acc;
}

// Early variant of the SCI32 kRemapColors kernel function, used in the demo of QFG4
reg_t kRemapColors(EngineState *s, int argc, reg_t *argv) {
	uint16 operation = argv[0].toUint16();

	switch (operation) {
	case 0: { // remap by percent
		uint16 percent = argv[1].toUint16();
		g_sci->_gfxRemap16->resetRemapping();
		g_sci->_gfxRemap16->setRemappingPercent(254, percent);
		}
		break;
	case 1:	{ // remap by range
		uint16 from = argv[1].toUint16();
		uint16 to = argv[2].toUint16();
		uint16 base = argv[3].toUint16();
		g_sci->_gfxRemap16->resetRemapping();
		g_sci->_gfxRemap16->setRemappingRange(254, from, to, base);
		}
		break;
	case 2:	// turn remapping off (unused)
		error("Unused subop kRemapColors(2) has been called");
		break;
	default:
		break;
	}

	return s->r_acc;
}

// Later SCI32-style kRemapColors, but in SCI11+.
reg_t kRemapColorsKawa(EngineState *s, int argc, reg_t *argv) {
	uint16 operation = argv[0].toUint16();

	switch (operation) {
	case 0: // off
		break;
	case 1: { // remap by percent
		uint16 from = argv[1].toUint16();
		uint16 percent = argv[2].toUint16();
		g_sci->_gfxRemap16->resetRemapping();
		g_sci->_gfxRemap16->setRemappingPercent(from, percent);
		}
		break;
	case 2: { // remap by range
		uint16 from = argv[1].toUint16();
		uint16 to = argv[2].toUint16();
		uint16 base = argv[3].toUint16();
		g_sci->_gfxRemap16->resetRemapping();
		g_sci->_gfxRemap16->setRemappingRange(254, from, to, base);
		}
		break;
	default:
		error("Unsupported SCI32-style kRemapColors(%d) has been called", operation);
		break;
	}
	return s->r_acc;
}

} // End of namespace Sci
