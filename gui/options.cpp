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

#include "gui/browser.h"
#include "gui/themebrowser.h"
#include "gui/message.h"
#include "gui/gui-manager.h"
#include "gui/options.h"
#include "gui/widgets/popup.h"
#include "gui/widgets/tab.h"
#include "gui/ThemeEval.h"
#include "gui/launcher.h"

#include "backends/keymapper/keymapper.h"
#include "backends/keymapper/remap-widget.h"

#include "common/fs.h"
#include "common/config-manager.h"
#include "common/gui_options.h"
#include "common/rendermode.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/translation.h"
#include "common/updates.h"
#include "common/util.h"
#include "common/text-to-speech.h"

#include "engines/achievements.h"

#include "audio/mididrv.h"
#include "audio/musicplugin.h"
#include "audio/mixer.h"
#include "audio/fmopl.h"
#include "widgets/scrollcontainer.h"
#include "widgets/edittext.h"

#ifdef USE_CLOUD
#ifdef USE_LIBCURL
#include "backends/cloud/cloudmanager.h"
#include "gui/downloaddialog.h"
#include "gui/downloadiconsdialog.h"
#endif

#ifdef USE_SDL_NET
#include "backends/networking/sdl_net/localwebserver.h"
#endif
#endif

#include "graphics/palette.h"
#include "graphics/pm5544.h"
#include "graphics/renderer.h"
#include "graphics/scalerplugin.h"

namespace GUI {

enum {
	kMidiGainChanged		= 'mgch',
	kMusicVolumeChanged		= 'muvc',
	kSfxVolumeChanged		= 'sfvc',
	kMuteAllChanged			= 'mute',
	kSubtitleToggle			= 'sttg',
	kSubtitleSpeedChanged	= 'stsc',
	kSpeechVolumeChanged	= 'vcvc',
	kChooseSoundFontCmd		= 'chsf',
	kClearSoundFontCmd      = 'clsf',
	kChooseSaveDirCmd		= 'chos',
	kSavePathClearCmd		= 'clsp',
	kChooseThemeDirCmd		= 'chth',
	kChooseIconDirCmd		= 'chic',
	kThemePathClearCmd		= 'clth',
	kBrowserPathClearCmd	= 'clbr',
	kIconPathClearCmd		= 'clic',
	kChooseExtraDirCmd		= 'chex',
	kExtraPathClearCmd		= 'clex',
	kChoosePluginsDirCmd	= 'chpl',
	kPluginsPathClearCmd	= 'clpl',
	kChooseThemeCmd			= 'chtf',
	kUpdateIconsCmd			= 'upic',
	kChooseShaderCmd        = 'chsh',
	kClearShaderCmd         = 'clsh',
	kUpdatesCheckCmd		= 'updc',
	kKbdMouseSpeedChanged	= 'kmsc',
	kJoystickDeadzoneChanged= 'jodc',
	kGraphicsTabContainerReflowCmd = 'gtcr',
	kScalerPopUpCmd			= 'scPU',
	kFullscreenToggled		= 'oful',
};

enum {
	kSubtitlesSpeech,
	kSubtitlesSubs,
	kSubtitlesBoth,
};

#ifdef USE_FLUIDSYNTH
enum {
	kFluidSynthSettingsCmd  = 'flst'
};
#endif

#ifdef USE_CLOUD
enum {
	kStoragePopUpCmd = 'sPup',
	kSyncSavesStorageCmd = 'ssst',
	kDownloadStorageCmd = 'dlst',
	kRunServerCmd = 'rnsv',
	kCloudTabContainerReflowCmd = 'ctcr',
	kServerPortClearCmd = 'spcl',
	kChooseRootDirCmd = 'chrp',
	kRootPathClearCmd = 'clrp',
	kConnectStorageCmd = 'Cnnt',
	kOpenUrlStorageCmd = 'OpUr',
	kPasteCodeStorageCmd = 'PsCd',
	kDisconnectStorageCmd = 'DcSt',
	kEnableStorageCmd = 'EnSt'
};
#endif

enum {
	kApplyCmd = 'appl'
};

static const char *savePeriodLabels[] = { _s("Never"), _s("Every 5 mins"), _s("Every 10 mins"), _s("Every 15 mins"), _s("Every 30 mins"), nullptr };
static const int savePeriodValues[] = { 0, 5 * 60, 10 * 60, 15 * 60, 30 * 60, -1 };

static const char *guiBaseLabels[] = {
	// I18N: Very large GUI scale
	_s("Very large"),
	// I18N: Large GUI scale
	_s("Large"),
	// I18N: Medium GUI scale
	_s("Medium"),
	// I18N: FunKey S GUI scale
	_s("FunKey S"),
	// I18N: Small GUI scale
	_s("Small"),
	nullptr
};
static const int guiBaseValues[] = { 150, 125, 100, 90, 75, -1 };

// The keyboard mouse speed values range from 0 to 7 and correspond to speeds shown in the label
// "10" (value 3) is the default speed corresponding to the speed before introduction of this control
static const char *kbdMouseSpeedLabels[] = { "3", "5", "8", "10", "13", "15", "18", "20", nullptr };

OptionsDialog::OptionsDialog(const Common::String &domain, int x, int y, int w, int h)
	: Dialog(x, y, w, h), _domain(domain), _graphicsTabId(-1), _midiTabId(-1), _pathsTabId(-1), _tabWidget(nullptr) {
	init();
}

OptionsDialog::OptionsDialog(const Common::String &domain, const Common::String &name)
	: Dialog(name), _domain(domain), _graphicsTabId(-1), _midiTabId(-1), _pathsTabId(-1), _tabWidget(nullptr) {
	init();
}

OptionsDialog::~OptionsDialog() {
	delete _subToggleGroup;
}

void OptionsDialog::init() {
	_enableControlSettings = false;
	_touchpadCheckbox = nullptr;
	_kbdMouseSpeedDesc = nullptr;
	_kbdMouseSpeedSlider = nullptr;
	_kbdMouseSpeedLabel = nullptr;
	_joystickDeadzoneDesc = nullptr;
	_joystickDeadzoneSlider = nullptr;
	_joystickDeadzoneLabel = nullptr;
	_keymapperWidget = nullptr;
	_backendOptions = nullptr;
	_enableGraphicSettings = false;
	_gfxPopUp = nullptr;
	_gfxPopUpDesc = nullptr;
	_renderModePopUp = nullptr;
	_renderModePopUpDesc = nullptr;
	_stretchPopUp = nullptr;
	_stretchPopUpDesc = nullptr;
	_scalerPopUp = nullptr;
	_scalerPopUpDesc = nullptr;
	_scaleFactorPopUp = nullptr;
	_fullscreenCheckbox = nullptr;
	_filteringCheckbox = nullptr;
	_aspectCheckbox = nullptr;
	_shader = nullptr;
	_shaderButton = nullptr;
	_shaderClearButton = nullptr;
	_vsyncCheckbox = nullptr;
	_rendererTypePopUpDesc = nullptr;
	_rendererTypePopUp = nullptr;
	_antiAliasPopUpDesc = nullptr;
	_antiAliasPopUp = nullptr;
	_enableAudioSettings = false;
	_midiPopUp = nullptr;
	_midiPopUpDesc = nullptr;
	_oplPopUp = nullptr;
	_oplPopUpDesc = nullptr;
	_enableMIDISettings = false;
	_gmDevicePopUp = nullptr;
	_gmDevicePopUpDesc = nullptr;
	_soundFont = nullptr;
	_soundFontButton = nullptr;
	_soundFontClearButton = nullptr;
	_multiMidiCheckbox = nullptr;
	_midiGainDesc = nullptr;
	_midiGainSlider = nullptr;
	_midiGainLabel = nullptr;
	_enableMT32Settings = false;
	_mt32Checkbox = nullptr;
	_mt32DevicePopUp = nullptr;
	_mt32DevicePopUpDesc = nullptr;
	_enableGSCheckbox = nullptr;
	_enableVolumeSettings = false;
	_musicVolumeDesc = nullptr;
	_musicVolumeSlider = nullptr;
	_musicVolumeLabel = nullptr;
	_sfxVolumeDesc = nullptr;
	_sfxVolumeSlider = nullptr;
	_sfxVolumeLabel = nullptr;
	_speechVolumeDesc = nullptr;
	_speechVolumeSlider = nullptr;
	_speechVolumeLabel = nullptr;
	_muteCheckbox = nullptr;
	_enableSubtitleSettings = false;
	_enableSubtitleToggle = false;
	_subToggleDesc = nullptr;
	_subToggleGroup = nullptr;
	_subToggleSubOnly = nullptr;
	_subToggleSpeechOnly = nullptr;
	_subToggleSubBoth = nullptr;
	_subSpeedDesc = nullptr;
	_subSpeedSlider = nullptr;
	_subSpeedLabel = nullptr;

	// Retrieve game GUI options
	_guioptions.clear();
	if (ConfMan.hasKey("guioptions", _domain)) {
		_guioptionsString = ConfMan.get("guioptions", _domain);

		const Plugin *plugin = nullptr;
		EngineMan.findTarget(_domain, &plugin);
		if (plugin) {
			const MetaEngineDetection &metaEngineDetection = plugin->get<MetaEngineDetection>();
			_guioptions = metaEngineDetection.parseAndCustomizeGuiOptions(_guioptionsString, _domain);
		} else {
			_guioptions = parseGameGUIOptions(_guioptionsString);
		}
	}
}

void OptionsDialog::build() {
	// Retrieve game GUI options
	_guioptions.clear();
	if (ConfMan.hasKey("guioptions", _domain)) {
		_guioptionsString = ConfMan.get("guioptions", _domain);

		const Plugin *plugin = nullptr;
		EngineMan.findTarget(_domain, &plugin);
		if (plugin) {
			const MetaEngineDetection &metaEngineDetection = plugin->get<MetaEngineDetection>();
			_guioptions = metaEngineDetection.parseAndCustomizeGuiOptions(_guioptionsString, _domain);
		} else {
			_guioptions = parseGameGUIOptions(_guioptionsString);
		}
	}

	// Control options
	if (g_system->hasFeature(OSystem::kFeatureTouchpadMode)) {
		if (ConfMan.hasKey("touchpad_mouse_mode", _domain)) {
			bool touchpadState =  g_system->getFeatureState(OSystem::kFeatureTouchpadMode);
			if (_touchpadCheckbox != nullptr)
				_touchpadCheckbox->setState(touchpadState);
		}
	}
	if (g_system->hasFeature(OSystem::kFeatureKbdMouseSpeed)) {
		int value = ConfMan.getInt("kbdmouse_speed", _domain);
		if (_kbdMouseSpeedSlider && value < ARRAYSIZE(kbdMouseSpeedLabels) - 1 && value >= 0) {
			_kbdMouseSpeedSlider->setValue(value);
			_kbdMouseSpeedLabel->setLabel(_(kbdMouseSpeedLabels[value]));
		}
	}
	if (g_system->hasFeature(OSystem::kFeatureJoystickDeadzone)) {
		int value = ConfMan.getInt("joystick_deadzone", _domain);
		if (_joystickDeadzoneSlider != nullptr) {
			_joystickDeadzoneSlider->setValue(value);
			_joystickDeadzoneLabel->setValue(value);
		}
	}

	// Keymapper options
	if (_keymapperWidget) {
		_keymapperWidget->load();
	}

	// Backend options
	if (_backendOptions) {
		_backendOptions->load();
	}

	// Graphic options
	if (_fullscreenCheckbox) {
		_gfxPopUp->setSelected(0);

		if (ConfMan.hasKey("gfx_mode", _domain)) {
			const OSystem::GraphicsMode *gm = g_system->getSupportedGraphicsModes();
			Common::String gfxMode(ConfMan.get("gfx_mode", _domain));
			int gfxCount = 1;
			while (gm->name) {
				gfxCount++;

				if (scumm_stricmp(gm->name, gfxMode.c_str()) == 0)
					_gfxPopUp->setSelected(gfxCount);

				gm++;
			}
		}

		_renderModePopUp->setSelected(0);

		if (ConfMan.hasKey("render_mode", _domain)) {
			const Common::RenderModeDescription *p = Common::g_renderModes;
			const Common::RenderMode renderMode = Common::parseRenderMode(ConfMan.get("render_mode", _domain));
			int sel = 0;
			for (int i = 0; p->code; ++p, ++i) {
				if (renderMode == p->id)
					sel = p->id;
			}
			_renderModePopUp->setSelectedTag(sel);
		}

		_stretchPopUp->setSelected(0);

		if (g_system->hasFeature(OSystem::kFeatureStretchMode)) {
			if (ConfMan.hasKey("stretch_mode", _domain)) {
				const OSystem::GraphicsMode *sm = g_system->getSupportedStretchModes();
				Common::String stretchMode(ConfMan.get("stretch_mode", _domain));
				int stretchCount = 1;
				while (sm->name) {
					stretchCount++;
					if (scumm_stricmp(sm->name, stretchMode.c_str()) == 0)
						_stretchPopUp->setSelected(stretchCount);
					sm++;
				}
			}
		} else {
			_stretchPopUpDesc->setVisible(false);
			_stretchPopUp->setVisible(false);
			_stretchPopUp->setEnabled(false);
		}

		_scalerPopUp->setSelected(0);
		_scaleFactorPopUp->setSelected(0);

		if (g_system->hasFeature(OSystem::kFeatureScalers)) {
			if (ConfMan.hasKey("scaler", _domain)) {
				const PluginList &scalerPlugins = ScalerMan.getPlugins();
				Common::String scaler(ConfMan.get("scaler", _domain));

				for (uint scalerIndex = 0; scalerIndex < scalerPlugins.size(); scalerIndex++) {
					if (scumm_stricmp(scalerPlugins[scalerIndex]->get<ScalerPluginObject>().getName(), scaler.c_str()) != 0)
						continue;

					_scalerPopUp->setSelectedTag(scalerIndex);
					updateScaleFactors(scalerIndex);

					if (ConfMan.hasKey("scale_factor", _domain)) {
						int scaleFactor = ConfMan.getInt("scale_factor", _domain);
						if (scalerPlugins[scalerIndex]->get<ScalerPluginObject>().hasFactor(scaleFactor))
							_scaleFactorPopUp->setSelectedTag(scaleFactor);
					}

					break;
				}

			}
		} else {
			_scalerPopUpDesc->setVisible(false);
			_scalerPopUp->setVisible(false);
			_scalerPopUp->setEnabled(false);
			_scaleFactorPopUp->setVisible(false);
			_scaleFactorPopUp->setEnabled(false);
		}

		// Fullscreen setting
		if (g_system->hasFeature(OSystem::kFeatureFullscreenMode)) {
			_fullscreenCheckbox->setState(ConfMan.getBool("fullscreen", _domain));
			if (ConfMan.isKeyTemporary("fullscreen"))
				_fullscreenCheckbox->setOverride(true);
		} else {
			_fullscreenCheckbox->setState(true);
			_fullscreenCheckbox->setEnabled(false);
		}

		// Filtering setting
		if (g_system->hasFeature(OSystem::kFeatureFilteringMode)) {
			_filteringCheckbox->setState(ConfMan.getBool("filtering", _domain));
			if (ConfMan.isKeyTemporary("filtering"))
				_filteringCheckbox->setOverride(true);
		}

		// Aspect ratio setting
		if (_guioptions.contains(GUIO_NOASPECT)) {
			_aspectCheckbox->setState(false);
			_aspectCheckbox->setEnabled(false);
		} else {
			_aspectCheckbox->setEnabled(true);
			_aspectCheckbox->setState(ConfMan.getBool("aspect_ratio", _domain));
		}

		_vsyncCheckbox->setState(ConfMan.getBool("vsync", _domain));

		_rendererTypePopUp->setEnabled(true);
		_rendererTypePopUp->setSelectedTag(Graphics::Renderer::parseTypeCode(ConfMan.get("renderer", _domain)));

		_antiAliasPopUp->setEnabled(true);
		if (ConfMan.hasKey("antialiasing", _domain)) {
			_antiAliasPopUp->setSelectedTag(ConfMan.getInt("antialiasing", _domain));
		} else {
			_antiAliasPopUp->setSelectedTag(uint32(-1));
		}
	}

	// Shader options
	if (_shader) {
		if (g_system->hasFeature(OSystem::kFeatureShaders)) {
			Common::String shader(ConfMan.get("shader", _domain));
			if (ConfMan.isKeyTemporary("shader")) {
				_shader->setFontColor(ThemeEngine::FontColor::kFontColorOverride);
			}
			if (shader.empty() || shader == "default" || !ConfMan.hasKey("shader", _domain)) {
				_shader->setLabel(_c("None", "shader"));
				_shaderClearButton->setEnabled(false);
			} else {
				_shader->setLabel(shader);
				_shaderClearButton->setEnabled(true);
			}
		} else {
			_shader->setVisible(false);
			_shaderButton->setVisible(false);
			_shaderClearButton->setVisible(false);
		}
	}

	// Audio options
	if (!loadMusicDeviceSetting(_midiPopUp, "music_driver"))
		_midiPopUp->setSelected(0);

	if (_oplPopUp) {
		OPL::Config::DriverId id = MAX<OPL::Config::DriverId>(OPL::Config::parse(ConfMan.get("opl_driver", _domain)), 0);
		_oplPopUp->setSelectedTag(id);
	}

	if (_multiMidiCheckbox) {
		if (!loadMusicDeviceSetting(_gmDevicePopUp, "gm_device"))
			_gmDevicePopUp->setSelected(0);

		// Multi midi setting
		_multiMidiCheckbox->setState(ConfMan.getBool("multi_midi", _domain));
		if (ConfMan.isKeyTemporary("multi_midi"))
			_multiMidiCheckbox->setOverride(true);

		Common::String soundFont(ConfMan.get("soundfont", _domain));
		if (ConfMan.isKeyTemporary("soundfont")) {
			_soundFont->setFontColor(ThemeEngine::FontColor::kFontColorOverride);
		}
		if (soundFont.empty() || !ConfMan.hasKey("soundfont", _domain)) {
			_soundFont->setLabel(_c("None", "soundfont"));
			_soundFontClearButton->setEnabled(false);
		} else {
			_soundFont->setLabel(soundFont);
			_soundFontClearButton->setEnabled(true);
		}

		// MIDI gain setting
		_midiGainSlider->setValue(ConfMan.getInt("midi_gain", _domain));
		_midiGainLabel->setLabel(Common::String::format("%.2f", (double)_midiGainSlider->getValue() / 100.0));
		if (ConfMan.isKeyTemporary("midi_gain"))
			_midiGainDesc->setFontColor(ThemeEngine::FontColor::kFontColorOverride);
	}

	// MT-32 options
	if (_mt32DevicePopUp) {
		if (!loadMusicDeviceSetting(_mt32DevicePopUp, "mt32_device"))
			_mt32DevicePopUp->setSelected(0);

		// Native mt32 setting
		_mt32Checkbox->setState(ConfMan.getBool("native_mt32", _domain));
		if (ConfMan.isKeyTemporary("native_mt32"))
			_mt32Checkbox->setOverride(true);

		// GS extensions setting
		_enableGSCheckbox->setState(ConfMan.getBool("enable_gs", _domain));
		if (ConfMan.isKeyTemporary("enable_gs"))
			_enableGSCheckbox->setOverride(true);
	}

	// Volume options
	if (_musicVolumeSlider) {
		int vol;

		vol = ConfMan.getInt("music_volume", _domain);
		_musicVolumeSlider->setValue(vol);
		_musicVolumeLabel->setValue(vol);

		vol = ConfMan.getInt("sfx_volume", _domain);
		_sfxVolumeSlider->setValue(vol);
		_sfxVolumeLabel->setValue(vol);

		vol = ConfMan.getInt("speech_volume", _domain);
		_speechVolumeSlider->setValue(vol);
		_speechVolumeLabel->setValue(vol);

		bool val = false;
		if (ConfMan.hasKey("mute", _domain)) {
			val = ConfMan.getBool("mute", _domain);
		} else {
			ConfMan.setBool("mute", false);
		}
		_muteCheckbox->setState(val);
	}

	// Subtitle options
	if (_subToggleGroup) {
		int speed;
		int sliderMaxValue = _subSpeedSlider->getMaxValue();

		int subMode = getSubtitleMode(ConfMan.getBool("subtitles", _domain), ConfMan.getBool("speech_mute", _domain));
		_subToggleGroup->setValue(subMode);

		// Engines that reuse the subtitle speed widget set their own max value.
		// Scale the config value accordingly (see addSubtitleControls)
		speed = (ConfMan.getInt("talkspeed", _domain) * sliderMaxValue + 255 / 2) / 255;
		_subSpeedSlider->setValue(speed);
		_subSpeedLabel->setValue(speed);
	}
}

void OptionsDialog::clean() {
	delete _subToggleGroup;
	while (_firstWidget) {
		Widget* w = _firstWidget;
		removeWidget(w);
		// This is called from rebuild() which may result from handleCommand being called by
		// a child widget sendCommand call. In such a case sendCommand is still being executed
		// so we should not delete yet the child widget. Thus delay the deletion.
		g_gui.addToTrash(w, this);
	}
	init();
}

void OptionsDialog::rebuild() {
	int currentTab = _tabWidget->getActiveTab();
	clean();
	build();
	reflowLayout();
	_tabWidget->setActiveTab(currentTab);
	setDefaultFocusedWidget();
}

void OptionsDialog::open() {
	build();

	Dialog::open();

	// Reset result value
	setResult(0);
}

void OptionsDialog::apply() {
	bool graphicsModeChanged = false;

	// Graphic options
	if (_fullscreenCheckbox) {
		if (_enableGraphicSettings) {
			if (g_system->hasFeature(OSystem::kFeatureFilteringMode))
				if (ConfMan.getBool("filtering", _domain) != _filteringCheckbox->getState()) {
					graphicsModeChanged = true;
					ConfMan.setBool("filtering", _filteringCheckbox->getState(), _domain);
					_filteringCheckbox->setOverride(false);
				}
			if (ConfMan.getBool("fullscreen", _domain) != _fullscreenCheckbox->getState()) {
				graphicsModeChanged = true;
				ConfMan.setBool("fullscreen", _fullscreenCheckbox->getState(), _domain);
				_fullscreenCheckbox->setOverride(false);
			}
			if (ConfMan.getBool("aspect_ratio", _domain) != _aspectCheckbox->getState())
				graphicsModeChanged = true;
			if (ConfMan.getBool("vsync", _domain) != _vsyncCheckbox->getState())
				graphicsModeChanged = true;

			ConfMan.setBool("aspect_ratio", _aspectCheckbox->getState(), _domain);
			ConfMan.setBool("vsync", _vsyncCheckbox->getState(), _domain);

			bool isSet = false;

			if ((int32)_gfxPopUp->getSelectedTag() >= 0) {
				const OSystem::GraphicsMode *gm = g_system->getSupportedGraphicsModes();

				while (gm->name) {
					if (gm->id == (int)_gfxPopUp->getSelectedTag()) {
						if (ConfMan.get("gfx_mode", _domain) != gm->name) {
							_gfxPopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
							graphicsModeChanged = true;
							ConfMan.set("gfx_mode", gm->name, _domain);
						}
						isSet = true;
						break;
					}
					gm++;
				}
			}
			if (!isSet) {
				_gfxPopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
				ConfMan.removeKey("gfx_mode", _domain);
				if (g_system->getGraphicsMode() != g_system->getDefaultGraphicsMode())
					graphicsModeChanged = true;
			}

			if ((int32)_renderModePopUp->getSelectedTag() >= 0) {
				Common::String renderModeCode = Common::getRenderModeCode((Common::RenderMode)_renderModePopUp->getSelectedTag());
				if (_renderModePopUp->getSelectedTag() == 0 || ConfMan.get("render_mode", _domain) != renderModeCode) {
					ConfMan.set("render_mode", renderModeCode, _domain);
					_renderModePopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
				}
			}

			isSet = false;
			if ((int32)_stretchPopUp->getSelectedTag() >= 0) {
				const OSystem::GraphicsMode *sm = g_system->getSupportedStretchModes();

				while (sm->name) {
					if (sm->id == (int)_stretchPopUp->getSelectedTag()) {
						if (ConfMan.get("stretch_mode", _domain) != sm->name) {
							graphicsModeChanged = true;
							ConfMan.set("stretch_mode", sm->name, _domain);
							_stretchPopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
						}
						isSet = true;
						break;
					}
					sm++;
				}
			}
			if (!isSet) {
				_stretchPopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
				ConfMan.removeKey("stretch_mode", _domain);
				if (g_system->getStretchMode() != g_system->getDefaultStretchMode())
					graphicsModeChanged = true;
			}

			isSet = false;
			const PluginList &scalerPlugins = ScalerMan.getPlugins();
			if ((int32)_scalerPopUp->getSelectedTag() >= 0) {
				const char *name = scalerPlugins[_scalerPopUp->getSelectedTag()]->get<ScalerPluginObject>().getName();
				if (ConfMan.get("scaler", _domain) != name) {
					graphicsModeChanged = true;
					ConfMan.set("scaler", name, _domain);
					_scalerPopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
				}

				int factor = _scaleFactorPopUp->getSelectedTag();
				if (ConfMan.getInt("scale_factor", _domain) != factor) {
					ConfMan.setInt("scale_factor", factor, _domain);
					graphicsModeChanged = true;
					_scalerPopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
				}
				isSet = true;
			}
			if (!isSet) {
				ConfMan.removeKey("scaler", _domain);
				ConfMan.removeKey("scale_factor", _domain);
				_scalerPopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);

				uint defaultScaler = g_system->getDefaultScaler();
				uint defaultScaleFactor = g_system->getDefaultScaleFactor();
				if (g_system->getScaler() != defaultScaler)
					graphicsModeChanged = true;
				else if (g_system->getScaleFactor() != defaultScaleFactor)
					graphicsModeChanged = true;
			}

			if (_rendererTypePopUp->getSelectedTag() > 0) {
				Graphics::RendererType selected = (Graphics::RendererType) _rendererTypePopUp->getSelectedTag();
				ConfMan.set("renderer", Graphics::Renderer::getTypeCode(selected), _domain);
			} else {
				ConfMan.removeKey("renderer", _domain);
			}

			if (_antiAliasPopUp->getSelectedTag() != (uint32)-1) {
				uint level = _antiAliasPopUp->getSelectedTag();
				ConfMan.setInt("antialiasing", level, _domain);
			} else {
				ConfMan.removeKey("antialiasing", _domain);
			}

		} else {
			ConfMan.removeKey("fullscreen", _domain);
			ConfMan.removeKey("filtering", _domain);
			ConfMan.removeKey("aspect_ratio", _domain);
			ConfMan.removeKey("gfx_mode", _domain);
			ConfMan.removeKey("stretch_mode", _domain);
			ConfMan.removeKey("scaler", _domain);
			ConfMan.removeKey("scale_factor", _domain);
			ConfMan.removeKey("render_mode", _domain);
			ConfMan.removeKey("renderer", _domain);
			ConfMan.removeKey("antialiasing", _domain);
			ConfMan.removeKey("vsync", _domain);
		}
	}

	Common::U32String previousShader;

	// Shader options
	if (_shader) {
		if (ConfMan.hasKey("shader", _domain) && !ConfMan.get("shader", _domain).empty())
			previousShader = ConfMan.get("shader", _domain);

		Common::U32String shader(_shader->getLabel());

		if (shader == _c("None", "shader"))
			shader = "default";

		if (!ConfMan.hasKey("shader", _domain) || shader != ConfMan.get("shader", _domain))
			graphicsModeChanged = true;

		if (_enableGraphicSettings)
			ConfMan.set("shader", shader.encode(), _domain);
		else
			ConfMan.removeKey("shader", _domain);

		_shader->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
	}

	// Setup graphics again if needed
	if (_domain == Common::ConfigManager::kApplicationDomain && graphicsModeChanged) {
		g_system->beginGFXTransaction();
		g_system->setGraphicsMode(ConfMan.get("gfx_mode", _domain).c_str());
		g_system->setStretchMode(ConfMan.get("stretch_mode", _domain).c_str());
		g_system->setScaler(ConfMan.get("scaler", _domain).c_str(), ConfMan.getInt("scale_factor", _domain));
		g_system->setShader(ConfMan.get("shader", _domain));

		if (ConfMan.hasKey("aspect_ratio"))
			g_system->setFeatureState(OSystem::kFeatureAspectRatioCorrection, ConfMan.getBool("aspect_ratio", _domain));
		if (ConfMan.hasKey("fullscreen"))
			g_system->setFeatureState(OSystem::kFeatureFullscreenMode, ConfMan.getBool("fullscreen", _domain));
		if (ConfMan.hasKey("filtering"))
			g_system->setFeatureState(OSystem::kFeatureFilteringMode, ConfMan.getBool("filtering", _domain));

		OSystem::TransactionError gfxError = g_system->endGFXTransaction();

		// Since this might change the screen resolution we need to give
		// the GUI a chance to update it's internal state. Otherwise we might
		// get a crash when the GUI tries to grab the overlay.
		//
		// This fixes bug #5703 "Switching from HQ2x->HQ3x crashes ScummVM"
		//
		// It is important that this is called *before* any of the current
		// dialog's widgets are destroyed (for example before
		// Dialog::close) is called, to prevent crashes caused by invalid
		// widgets being referenced or similar errors.
		g_gui.checkScreenChange();

		if (gfxError != OSystem::kTransactionSuccess) {
			// Revert ConfMan to what OSystem is using.
			Common::U32String message = _("Failed to apply some of the graphic options changes:");

			if (gfxError & OSystem::kTransactionModeSwitchFailed) {
				const OSystem::GraphicsMode *gm = g_system->getSupportedGraphicsModes();
				while (gm->name) {
					if (gm->id == g_system->getGraphicsMode()) {
						ConfMan.set("gfx_mode", gm->name, _domain);
						break;
					}
					gm++;
				}
				message += Common::U32String("\n");
				message += _("the video mode could not be changed");
			}

			if (gfxError & OSystem::kTransactionStretchModeSwitchFailed) {
				const OSystem::GraphicsMode *sm = g_system->getSupportedStretchModes();
				while (sm->name) {
					if (sm->id == g_system->getStretchMode()) {
						ConfMan.set("stretch_mode", sm->name, _domain);
						break;
					}
					sm++;
				}
				message += Common::U32String("\n");
				message += _("the stretch mode could not be changed");
			}

			if (gfxError & OSystem::kTransactionAspectRatioFailed) {
				ConfMan.setBool("aspect_ratio", g_system->getFeatureState(OSystem::kFeatureAspectRatioCorrection), _domain);
				message += Common::U32String("\n");
				message += _("the aspect ratio setting could not be changed");
			}

			if (gfxError & OSystem::kTransactionFullscreenFailed) {
				ConfMan.setBool("fullscreen", g_system->getFeatureState(OSystem::kFeatureFullscreenMode), _domain);
				message += Common::U32String("\n");
				message += _("the fullscreen setting could not be changed");
			}

			if (gfxError & OSystem::kTransactionFilteringFailed) {
				ConfMan.setBool("filtering", g_system->getFeatureState(OSystem::kFeatureFilteringMode), _domain);
				message += Common::U32String("\n");
				message += _("the filtering setting could not be changed");
			}

			if (gfxError & OSystem::kTransactionShaderChangeFailed) {
				if (previousShader == _c("None", "shader"))
					previousShader = "default";

				ConfMan.set("shader", previousShader.encode(), _domain);
				if (previousShader.empty()) {
					_shader->setLabel(_c("None", "shader"));
					_shaderClearButton->setEnabled(false);
				} else {
					_shader->setLabel(previousShader);
					_shaderClearButton->setEnabled(true);
				}

				message += Common::U32String("\n");
				message += _("the shader could not be changed");
			}

			// And display the error
			GUI::MessageDialog dialog(message);
			dialog.runModal();
		} else {
			// Successful transaction. Check if we need to show test screen
			Common::String shader;
			if (ConfMan.hasKey("shader", _domain))
				shader = ConfMan.get("shader", _domain);

			// If shader was changed, show the test dialog
			if (previousShader != shader && !shader.empty() && shader != "default") {
				if (!testGraphicsSettings()) {
					if (previousShader == _c("None", "shader"))
						previousShader = "default";

					ConfMan.set("shader", previousShader.encode(), _domain);
					if (previousShader.empty()) {
						_shader->setLabel(_c("None", "shader"));
						_shaderClearButton->setEnabled(false);
					} else {
						_shader->setLabel(previousShader);
						_shaderClearButton->setEnabled(true);
					}

					g_system->beginGFXTransaction();
					g_system->setShader(ConfMan.get("shader", _domain));
					g_system->endGFXTransaction();
				}
			}
		}
	}

	if (_keymapperWidget) {
		bool changes = _keymapperWidget->save();
		if (changes) {
			Common::Keymapper *keymapper = g_system->getEventManager()->getKeymapper();
			keymapper->reloadAllMappings();
		}
	}

	if (_backendOptions) {
		bool changes = _backendOptions->save();
		if (changes && _domain == Common::ConfigManager::kApplicationDomain)
			g_system->applyBackendSettings();
	}

	// Control options
	if (_enableControlSettings) {
		if (g_system->hasFeature(OSystem::kFeatureTouchpadMode)) {
			if (ConfMan.getBool("touchpad_mouse_mode", _domain) != _touchpadCheckbox->getState()) {
				g_system->setFeatureState(OSystem::kFeatureTouchpadMode, _touchpadCheckbox->getState());
			}
		}
		if (g_system->hasFeature(OSystem::kFeatureKbdMouseSpeed)) {
			if (ConfMan.getInt("kbdmouse_speed", _domain) != _kbdMouseSpeedSlider->getValue()) {
				ConfMan.setInt("kbdmouse_speed", _kbdMouseSpeedSlider->getValue(), _domain);
			}
		}
		if (g_system->hasFeature(OSystem::kFeatureJoystickDeadzone)) {
			if (ConfMan.getInt("joystick_deadzone", _domain) != _joystickDeadzoneSlider->getValue()) {
				ConfMan.setInt("joystick_deadzone", _joystickDeadzoneSlider->getValue(), _domain);
			}
		}
	}

	// Volume options
	if (_musicVolumeSlider) {
		if (_enableVolumeSettings) {
			ConfMan.setInt("music_volume", _musicVolumeSlider->getValue(), _domain);
			ConfMan.setInt("sfx_volume", _sfxVolumeSlider->getValue(), _domain);
			ConfMan.setInt("speech_volume", _speechVolumeSlider->getValue(), _domain);
			ConfMan.setBool("mute", _muteCheckbox->getState(), _domain);
		} else {
			ConfMan.removeKey("music_volume", _domain);
			ConfMan.removeKey("sfx_volume", _domain);
			ConfMan.removeKey("speech_volume", _domain);
			ConfMan.removeKey("mute", _domain);
		}
	}

	// Audio options
	if (_midiPopUp) {
		if (_enableAudioSettings) {
			saveMusicDeviceSetting(_midiPopUp, "music_driver");
		} else {
			ConfMan.removeKey("music_driver", _domain);
		}
	}

	if (_oplPopUp) {
		if (_enableAudioSettings) {
			const OPL::Config::EmulatorDescription *ed = OPL::Config::findDriver(_oplPopUp->getSelectedTag());

			if ((ed && ConfMan.get("opl_driver", _domain) != ed->name) || !ed) {
				_oplPopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
				if (ed)
					ConfMan.set("opl_driver", ed->name, _domain);
				else
					ConfMan.removeKey("opl_driver", _domain);
			}
		} else {
			_oplPopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
			ConfMan.removeKey("opl_driver", _domain);
		}
	}

	// MIDI options
	if (_multiMidiCheckbox) {
		if (_enableMIDISettings) {
			saveMusicDeviceSetting(_gmDevicePopUp, "gm_device");

			if (_multiMidiCheckbox->getState() != ConfMan.getBool("multi_midi", _domain)) {
				ConfMan.setBool("multi_midi", _multiMidiCheckbox->getState(), _domain);
				_multiMidiCheckbox->setOverride(false);
			}
			if (_midiGainSlider->getValue() != ConfMan.getInt("midi_gain", _domain)) {
				ConfMan.setInt("midi_gain", _midiGainSlider->getValue(), _domain);
				_midiGainDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
			}

			Common::U32String soundFont(_soundFont->getLabel());
			if (soundFont != ConfMan.get("soundfont", _domain)) {
				_soundFont->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
				if (soundFont.empty() || (soundFont == _c("None", "soundfont")))
					ConfMan.removeKey("soundfont", _domain);
				else
					ConfMan.set("soundfont", soundFont.encode(), _domain);
			}
		} else {
			ConfMan.removeKey("gm_device", _domain);
			ConfMan.removeKey("multi_midi", _domain);
			_multiMidiCheckbox->setOverride(false);
			ConfMan.removeKey("midi_gain", _domain);
			_midiGainDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
			ConfMan.removeKey("soundfont", _domain);
			_soundFont->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
		}
	}

	// MT-32 options
	if (_mt32DevicePopUp) {
		if (_enableMT32Settings) {
			saveMusicDeviceSetting(_mt32DevicePopUp, "mt32_device");
			ConfMan.setBool("native_mt32", _mt32Checkbox->getState(), _domain);
			if (ConfMan.getBool("native_mt32", _domain) != _mt32Checkbox->getState()) {
				ConfMan.setBool("native_mt32", _mt32Checkbox->getState(), _domain);
				_mt32Checkbox->setOverride(false);
			}
			if (ConfMan.getBool("enable_gs", _domain) != _enableGSCheckbox->getState()) {
				ConfMan.setBool("enable_gs", _enableGSCheckbox->getState(), _domain);
				_enableGSCheckbox->setOverride(false);
			}
		} else {
			ConfMan.removeKey("mt32_device", _domain);
			ConfMan.removeKey("native_mt32", _domain);
			_mt32Checkbox->setOverride(false);
			ConfMan.removeKey("enable_gs", _domain);
			_enableGSCheckbox->setOverride(false);
		}
	}

	// Subtitle options
	if (_subToggleGroup) {
		if (_enableSubtitleSettings) {
			if (_enableSubtitleToggle) {
				bool subtitles, speech_mute;
				switch (_subToggleGroup->getValue()) {
				case kSubtitlesSpeech:
					subtitles = speech_mute = false;
					break;
				case kSubtitlesBoth:
					subtitles = true;
					speech_mute = false;
					break;
				case kSubtitlesSubs:
				default:
					subtitles = speech_mute = true;
					break;
				}

				if (subtitles != ConfMan.getBool("subtitles", _domain)) {
					ConfMan.setBool("subtitles", subtitles, _domain);
					_subToggleDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
				}
				ConfMan.setBool("speech_mute", speech_mute, _domain);
			} else if (!_domain.empty()) {
				ConfMan.removeKey("subtitles", _domain);
				_subToggleDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
				ConfMan.removeKey("speech_mute", _domain);
			}

			// Engines that reuse the subtitle speed widget set their own max value.
			// Scale the config value accordingly (see addSubtitleControls)
			int sliderMaxValue = _subSpeedSlider->getMaxValue();
			int talkspeed = (_subSpeedSlider->getValue() * 255 + sliderMaxValue / 2) / sliderMaxValue;
			if (talkspeed != ConfMan.getInt("talkspeed", _domain)) {
				ConfMan.setInt("talkspeed", talkspeed, _domain);
				_subSpeedDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
			}
		} else {
			ConfMan.removeKey("subtitles", _domain);
			ConfMan.removeKey("talkspeed", _domain);
			_subSpeedDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
			ConfMan.removeKey("speech_mute", _domain);
		}
	}

	// Save config file
	ConfMan.flushToDisk();
}

void OptionsDialog::close() {
	if (getResult() > 0)
		apply();

	Dialog::close();
}

void OptionsDialog::handleCommand(CommandSender *sender, uint32 cmd, uint32 data) {
	switch (cmd) {
	case kClearShaderCmd:
		_shader->setLabel(_c("None", "shader"));
		_shaderClearButton->setEnabled(false);
		g_gui.scheduleTopDialogRedraw();
		break;
	case kMidiGainChanged:
		_midiGainLabel->setLabel(Common::String::format("%.2f", (double)_midiGainSlider->getValue() / 100.0));
		_midiGainLabel->markAsDirty();
		break;
	case kMusicVolumeChanged: {
		const int newValue = _musicVolumeSlider->getValue();
		_musicVolumeLabel->setValue(newValue);
		_musicVolumeLabel->markAsDirty();

		if (_guioptions.contains(GUIO_LINKMUSICTOSFX)) {
			updateSfxVolume(newValue);

			if (_guioptions.contains(GUIO_LINKSPEECHTOSFX)) {
				updateSpeechVolume(newValue);
			}
		}
		_musicVolumeDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
		break;
	}
	case kSfxVolumeChanged: {
		const int newValue = _sfxVolumeSlider->getValue();
		_sfxVolumeLabel->setValue(_sfxVolumeSlider->getValue());
		_sfxVolumeLabel->markAsDirty();

		if (_guioptions.contains(GUIO_LINKMUSICTOSFX)) {
			updateMusicVolume(newValue);
		}

		if (_guioptions.contains(GUIO_LINKSPEECHTOSFX)) {
			updateSpeechVolume(newValue);
		}
		_sfxVolumeDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
		break;
	}
	case kSpeechVolumeChanged: {
		const int newValue = _speechVolumeSlider->getValue();
		_speechVolumeLabel->setValue(newValue);
		_speechVolumeLabel->markAsDirty();

		if (_guioptions.contains(GUIO_LINKSPEECHTOSFX)) {
			updateSfxVolume(newValue);

			if (_guioptions.contains(GUIO_LINKMUSICTOSFX)) {
				updateMusicVolume(newValue);
			}
		}
		_speechVolumeDesc->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
		break;
	}
	case kMuteAllChanged:
		// 'true' because if control is disabled then event do not pass
		setVolumeSettingsState(true);
		break;
	case kSubtitleToggle:
		// We update the slider settings here, when there are sliders, to
		// disable the speech volume in case we are in subtitle only mode.
		if (_musicVolumeSlider)
			setVolumeSettingsState(true);
		break;
	case kSubtitleSpeedChanged:
		_subSpeedLabel->setValue(_subSpeedSlider->getValue());
		_subSpeedLabel->markAsDirty();
		break;
	case kClearSoundFontCmd:
		_soundFont->setLabel(_c("None", "soundfont"));
		_soundFontClearButton->setEnabled(false);
		g_gui.scheduleTopDialogRedraw();
		break;
	case kKbdMouseSpeedChanged:
		_kbdMouseSpeedLabel->setLabel(_(kbdMouseSpeedLabels[_kbdMouseSpeedSlider->getValue()]));
		_kbdMouseSpeedLabel->markAsDirty();
		break;
	case kJoystickDeadzoneChanged:
		_joystickDeadzoneLabel->setValue(_joystickDeadzoneSlider->getValue());
		_joystickDeadzoneLabel->markAsDirty();
		break;
	case kGraphicsTabContainerReflowCmd:
		setupGraphicsTab();
		break;
	case kScalerPopUpCmd:
		updateScaleFactors(data);
		g_gui.scheduleTopDialogRedraw();
		break;
	case kChooseShaderCmd: {
		BrowserDialog browser(_("Select shader"), false);
		if (browser.runModal() > 0) {
			// User made his choice...
			Common::FSNode file(browser.getResult());
			_shader->setLabel(file.getPath());

			if (!file.getPath().empty() && (file.getPath().decode() != _c("None", "path")))
				_shaderClearButton->setEnabled(true);
			else
				_shaderClearButton->setEnabled(false);

			g_gui.scheduleTopDialogRedraw();
		}
		break;
	}
	case kApplyCmd:
		apply();
		break;
	case kOKCmd:
		setResult(1);
		close();
		break;
	case kCloseCmd:
		close();
		break;
	default:
		Dialog::handleCommand(sender, cmd, data);
	}
}

void OptionsDialog::handleTickle() {
	Dialog::handleTickle();

	if (_keymapperWidget) {
		_keymapperWidget->handleTickle();
	}
}

void OptionsDialog::handleOtherEvent(const Common::Event &event) {
	Dialog::handleOtherEvent(event);

	if (event.type == Common::EVENT_INPUT_CHANGED && _keymapperWidget) {
		_keymapperWidget->handleInputChanged();
	}
}

void OptionsDialog::setGraphicSettingsState(bool enabled) {
	_enableGraphicSettings = enabled;

	_gfxPopUpDesc->setEnabled(enabled);
	_gfxPopUp->setEnabled(enabled);
	_renderModePopUpDesc->setEnabled(enabled);
	_renderModePopUp->setEnabled(enabled);
	_vsyncCheckbox->setEnabled(enabled);
	_rendererTypePopUpDesc->setEnabled(enabled);
	_rendererTypePopUp->setEnabled(enabled);
	_antiAliasPopUpDesc->setEnabled(enabled);
	_antiAliasPopUp->setEnabled(enabled);

	if (g_system->hasFeature(OSystem::kFeatureStretchMode)) {
		_stretchPopUpDesc->setEnabled(enabled);
		_stretchPopUp->setEnabled(enabled);
	} else {
		_stretchPopUpDesc->setEnabled(false);
		_stretchPopUp->setEnabled(false);
	}

	if (g_system->hasFeature(OSystem::kFeatureScalers)) {
		_scalerPopUpDesc->setEnabled(enabled);
		_scalerPopUp->setEnabled(enabled);
		_scaleFactorPopUp->setEnabled(enabled);
	} else {
		_scalerPopUpDesc->setEnabled(false);
		_scalerPopUp->setEnabled(false);
		_scaleFactorPopUp->setEnabled(false);
	}

	if (g_system->hasFeature(OSystem::kFeatureShaders)) {
		_shaderButton->setEnabled(enabled);
		_shader->setEnabled(enabled);
		_shaderClearButton->setEnabled(enabled);
	} else {
		// Happens when we switch to backend that doesn't support shaders
		if (_shader) {
			_shaderButton->setEnabled(false);
			_shader->setEnabled(false);
			_shaderClearButton->setEnabled(false);
		}
	}

	if (g_system->hasFeature(OSystem::kFeatureFilteringMode))
		_filteringCheckbox->setEnabled(enabled);

	if (g_system->hasFeature(OSystem::kFeatureFullscreenMode))
		_fullscreenCheckbox->setEnabled(enabled);
	else
		_fullscreenCheckbox->setEnabled(false);

	if (_guioptions.contains(GUIO_NOASPECT))
		_aspectCheckbox->setEnabled(false);
	else
		_aspectCheckbox->setEnabled(enabled);
}

void OptionsDialog::setAudioSettingsState(bool enabled) {
	_enableAudioSettings = enabled;
	_midiPopUpDesc->setEnabled(enabled);
	_midiPopUp->setEnabled(enabled);

	const Common::String allFlags = MidiDriver::musicType2GUIO((uint32)-1);
	bool hasMidiDefined = (strpbrk(_guioptions.c_str(), allFlags.c_str()) != nullptr);

	if (_domain != Common::ConfigManager::kApplicationDomain && // global dialog
		hasMidiDefined && // No flags are specified
		!(_guioptions.contains(GUIO_MIDIADLIB))) {
		_oplPopUpDesc->setEnabled(false);
		_oplPopUp->setEnabled(false);
	} else {
		_oplPopUpDesc->setEnabled(enabled);
		_oplPopUp->setEnabled(enabled);
	}
}

void OptionsDialog::setMIDISettingsState(bool enabled) {
	if (_guioptions.contains(GUIO_NOMIDI))
		enabled = false;

	_gmDevicePopUpDesc->setEnabled(_domain.equals(Common::ConfigManager::kApplicationDomain) ? enabled : false);
	_gmDevicePopUp->setEnabled(_domain.equals(Common::ConfigManager::kApplicationDomain) ? enabled : false);

	_enableMIDISettings = enabled;

	_soundFontButton->setEnabled(enabled);
	_soundFont->setEnabled(enabled);

	if (enabled && !_soundFont->getLabel().empty() && (_soundFont->getLabel() != _c("None", "soundfont")))
		_soundFontClearButton->setEnabled(enabled);
	else
		_soundFontClearButton->setEnabled(false);

	_multiMidiCheckbox->setEnabled(enabled);
	_midiGainDesc->setEnabled(enabled);
	_midiGainSlider->setEnabled(enabled);
	_midiGainLabel->setEnabled(enabled);
}

void OptionsDialog::setMT32SettingsState(bool enabled) {
	_enableMT32Settings = enabled;

	_mt32DevicePopUpDesc->setEnabled(_domain.equals(Common::ConfigManager::kApplicationDomain) ? enabled : false);
	_mt32DevicePopUp->setEnabled(_domain.equals(Common::ConfigManager::kApplicationDomain) ? enabled : false);

	_mt32Checkbox->setEnabled(enabled);
	_enableGSCheckbox->setEnabled(enabled);
}

void OptionsDialog::setVolumeSettingsState(bool enabled) {
	bool ena;

	_enableVolumeSettings = enabled;

	ena = enabled && !_muteCheckbox->getState();
	if (_guioptions.contains(GUIO_NOMUSIC))
		ena = false;

	_musicVolumeDesc->setEnabled(ena);
	_musicVolumeSlider->setEnabled(ena);
	_musicVolumeLabel->setEnabled(ena);

	ena = enabled && !_muteCheckbox->getState();
	if (_guioptions.contains(GUIO_NOSFX))
		ena = false;

	_sfxVolumeDesc->setEnabled(ena);
	_sfxVolumeSlider->setEnabled(ena);
	_sfxVolumeLabel->setEnabled(ena);

	ena = enabled && !_muteCheckbox->getState();
	// Disable speech volume slider, when we are in subtitle only mode.
	if (_subToggleGroup)
		ena = ena && _subToggleGroup->getValue() != kSubtitlesSubs;
	if (_guioptions.contains(GUIO_NOSPEECH) || _guioptions.contains(GUIO_NOSPEECHVOLUME))
		ena = false;

	_speechVolumeDesc->setEnabled(ena);
	_speechVolumeSlider->setEnabled(ena);
	_speechVolumeLabel->setEnabled(ena);

	_muteCheckbox->setEnabled(enabled);
}

void OptionsDialog::setSubtitleSettingsState(bool enabled) {
	bool ena;
	_enableSubtitleSettings = enabled;

	ena = enabled;
	if ((_guioptions.contains(GUIO_NOSUBTITLES)) || (_guioptions.contains(GUIO_NOSPEECH)))
		ena = false;

	_enableSubtitleToggle = ena;
	_subToggleGroup->setEnabled(ena);
	_subToggleDesc->setEnabled(ena);

	ena = enabled;
	if (_guioptions.contains(GUIO_NOSUBTITLES))
		ena = false;

	_subSpeedDesc->setEnabled(ena);
	_subSpeedSlider->setEnabled(ena);
	_subSpeedLabel->setEnabled(ena);
}

void OptionsDialog::addControlControls(GuiObject *boss, const Common::String &prefix) {
	// Touchpad Mouse mode
	if (g_system->hasFeature(OSystem::kFeatureTouchpadMode))
		_touchpadCheckbox = new CheckboxWidget(boss, prefix + "grTouchpadCheckbox", _("Touchpad mouse mode"));

	// Keyboard and joystick mouse speed
	if (g_system->hasFeature(OSystem::kFeatureKbdMouseSpeed)) {
		if (g_system->getOverlayWidth() > 320)
			_kbdMouseSpeedDesc = new StaticTextWidget(boss, prefix + "grKbdMouseSpeedDesc", _("Pointer Speed:"), _("Speed for keyboard/joystick mouse pointer control"));
		else
			_kbdMouseSpeedDesc = new StaticTextWidget(boss, prefix + "grKbdMouseSpeedDesc", _c("Pointer Speed:", "lowres"), _("Speed for keyboard/joystick mouse pointer control"));
		_kbdMouseSpeedSlider = new SliderWidget(boss, prefix + "grKbdMouseSpeedSlider", _("Speed for keyboard/joystick mouse pointer control"), kKbdMouseSpeedChanged);
		_kbdMouseSpeedLabel = new StaticTextWidget(boss, prefix + "grKbdMouseSpeedLabel", Common::U32String("  "), Common::U32String(), ThemeEngine::kFontStyleBold, Common::UNK_LANG, false);
		_kbdMouseSpeedSlider->setMinValue(0);
		_kbdMouseSpeedSlider->setMaxValue(7);
		_kbdMouseSpeedLabel->setFlags(WIDGET_CLEARBG);
	}

	// Joystick deadzone
	if (g_system->hasFeature(OSystem::kFeatureJoystickDeadzone)) {
		if (g_system->getOverlayWidth() > 320)
			_joystickDeadzoneDesc = new StaticTextWidget(boss, prefix + "grJoystickDeadzoneDesc", _("Joy Deadzone:"), _("Analog joystick Deadzone"));
		else
			_joystickDeadzoneDesc = new StaticTextWidget(boss, prefix + "grJoystickDeadzoneDesc", _c("Joy Deadzone:", "lowres"), _("Analog joystick Deadzone"));
		_joystickDeadzoneSlider = new SliderWidget(boss, prefix + "grJoystickDeadzoneSlider", _("Analog joystick Deadzone"), kJoystickDeadzoneChanged);
		_joystickDeadzoneLabel = new StaticTextWidget(boss, prefix + "grJoystickDeadzoneLabel", Common::U32String("  "), Common::U32String(), ThemeEngine::kFontStyleBold, Common::UNK_LANG, false);
		_joystickDeadzoneSlider->setMinValue(1);
		_joystickDeadzoneSlider->setMaxValue(10);
		_joystickDeadzoneLabel->setFlags(WIDGET_CLEARBG);
	}
	_enableControlSettings = true;
}

void OptionsDialog::addKeyMapperControls(GuiObject *boss, const Common::String &prefix, const Common::KeymapArray &keymaps, const Common::String &domain) {
	Common::Keymapper *mapper = g_system->getEventManager()->getKeymapper();
	for (uint i = 0; i < keymaps.size(); i++) {
		mapper->initKeymap(keymaps[i], ConfMan.getDomain(domain));
	}

	_keymapperWidget = new Common::RemapWidget(boss, prefix + "Container", keymaps);
}

void OptionsDialog::addAchievementsControls(GuiObject *boss, const Common::String &prefix) {
	GUI::ScrollContainerWidget *scrollContainer;
	scrollContainer = new GUI::ScrollContainerWidget(boss, prefix + "Container", "");
	scrollContainer->setBackgroundType(GUI::ThemeEngine::kWidgetBackgroundNo);

	uint16 nAchieved = 0;
	uint16 nHidden = 0;
	uint16 nMax = AchMan.getAchievementCount();

	uint16 lineHeight = g_gui.xmlEval()->getVar("Globals.Line.Height");
	uint16 yStep = lineHeight;
	uint16 ySmallStep = yStep / 3;
	uint16 yPos = lineHeight + yStep * 3;
	uint16 progressBarWidth = 240;
	uint16 width = g_system->getOverlayWidth() <= 320 ? 240 : 410;
	uint16 commentDelta = g_system->getOverlayWidth() <= 320 ? 25 : 30;

	for (int16 viewAchieved = 1; viewAchieved >= 0; viewAchieved--) {
		// run this twice, first view all achieved, then view all non-hidden & non-achieved

		for (uint16 idx = 0; idx < nMax ; idx++) {
			const Common::AchievementDescription *descr = AchMan.getAchievementDescription(idx);
			int16 isAchieved = AchMan.isAchieved(descr->id) ? 1 : 0;

			if (isAchieved != viewAchieved) {
				continue;
			}

			if (isAchieved) {
				nAchieved++;
			}

			if (!isAchieved && descr->isHidden) {
				nHidden++;
				continue;
			}

			CheckboxWidget *checkBox;
			checkBox = new CheckboxWidget(scrollContainer, lineHeight, yPos, width, yStep, Common::U32String(descr->title));
			checkBox->setEnabled(false);
			checkBox->setState(isAchieved);
			yPos += yStep;

	        if (!descr->comment.empty()) {
				new StaticTextWidget(scrollContainer, lineHeight + commentDelta, yPos, width - commentDelta, yStep, Common::U32String(descr->comment), Graphics::kTextAlignStart, Common::U32String(), ThemeEngine::kFontStyleNormal);
				yPos += yStep;
			}

			yPos += ySmallStep;
		}
	}

	if (nHidden) {
		Common::U32String hiddenStr = Common::U32String::format(_("%d hidden achievements remaining"), nHidden);
		new StaticTextWidget(scrollContainer, lineHeight, yPos, width, yStep, hiddenStr, Graphics::kTextAlignStart);
	}

	if (nMax) {
		Common::U32String totalStr = Common::U32String::format(_("Achievements unlocked: %d/%d"), nAchieved, nMax);
		new StaticTextWidget(scrollContainer, lineHeight, lineHeight, width, yStep, totalStr, Graphics::kTextAlignStart);

		SliderWidget *progressBar;
		progressBar = new SliderWidget(scrollContainer, lineHeight, lineHeight*2, progressBarWidth, lineHeight);
		progressBar->setMinValue(0);
		progressBar->setValue(nAchieved);
		progressBar->setMaxValue(nMax);
		progressBar->setEnabled(false);
	}
}

void OptionsDialog::addStatisticsControls(GuiObject *boss, const Common::String &prefix) {
	GUI::ScrollContainerWidget *scrollContainer;
	scrollContainer = new GUI::ScrollContainerWidget(boss, prefix + "Container", "");
	scrollContainer->setBackgroundType(GUI::ThemeEngine::kWidgetBackgroundNo);

	uint16 nMax = AchMan.getStatCount();

	uint16 lineHeight = g_gui.xmlEval()->getVar("Globals.Line.Height");
	uint16 yStep = lineHeight;
	uint16 ySmallStep = yStep / 3;
	uint16 yPos = lineHeight;
	uint16 width = g_system->getOverlayWidth() <= 320 ? 240 : 410;

	for (uint16 idx = 0; idx < nMax ; idx++) {
		const Common::StatDescription *descr = AchMan.getStatDescription(idx);

		const Common::String &key = descr->comment.empty() ? descr->id : descr->comment;
		Common::String value = AchMan.getStatRaw(descr->id);

		Common::U32String str = Common::U32String::format("%s: %s", key.c_str(), value.c_str());
		new StaticTextWidget(scrollContainer, lineHeight, yPos, width, yStep, str, Graphics::kTextAlignStart);

		yPos += yStep;
		yPos += ySmallStep;
	}
}

void OptionsDialog::addGraphicControls(GuiObject *boss, const Common::String &prefix) {
	const OSystem::GraphicsMode *gm = g_system->getSupportedGraphicsModes();
	Common::String context;
	if (g_system->getOverlayWidth() <= 320)
		context = "lowres";

	// The GFX mode popup
	_gfxPopUpDesc = new StaticTextWidget(boss, prefix + "grModePopupDesc", _("Graphics mode:"));
	if (ConfMan.isKeyTemporary("gfx_mode"))
		_gfxPopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorOverride);
	_gfxPopUp = new PopUpWidget(boss, prefix + "grModePopup");

	_gfxPopUp->appendEntry(_("<default>"));
	_gfxPopUp->appendEntry(Common::U32String());
	while (gm->name) {
		_gfxPopUp->appendEntry(_c(gm->description, context), gm->id);
		gm++;
	}

	// RenderMode popup
	const Common::String allFlags = Common::allRenderModesGUIOs();
	bool renderingTypeDefined = (strpbrk(_guioptions.c_str(), allFlags.c_str()) != nullptr);

	_renderModePopUpDesc = new StaticTextWidget(boss, prefix + "grRenderPopupDesc", _("Render mode:"), _("Special dithering modes supported by some games"));
	if (ConfMan.isKeyTemporary("render_mode"))
		_renderModePopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorOverride);
	_renderModePopUp = new PopUpWidget(boss, prefix + "grRenderPopup", _("Special dithering modes supported by some games"));
	_renderModePopUp->appendEntry(_("<default>"), Common::kRenderDefault);
	_renderModePopUp->appendEntry(Common::U32String());
	const Common::RenderModeDescription *rm = Common::g_renderModes;
	for (; rm->code; ++rm) {
		Common::String renderGuiOption = Common::renderMode2GUIO(rm->id);
		if ((_domain == Common::ConfigManager::kApplicationDomain) || (_domain != Common::ConfigManager::kApplicationDomain && renderingTypeDefined && _guioptions.contains(renderGuiOption)))
			_renderModePopUp->appendEntry(_c(rm->description, context), rm->id);
	}

	// The Stretch mode popup
	const OSystem::GraphicsMode *sm = g_system->getSupportedStretchModes();
	_stretchPopUpDesc = new StaticTextWidget(boss, prefix + "grStretchModePopupDesc", _("Stretch mode:"));
	if (ConfMan.isKeyTemporary("stretch_mode"))
		_stretchPopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorOverride);
	_stretchPopUp = new PopUpWidget(boss, prefix + "grStretchModePopup");

	_stretchPopUp->appendEntry(_("<default>"));
	_stretchPopUp->appendEntry(Common::U32String());
	while (sm->name) {
		_stretchPopUp->appendEntry(_c(sm->description, context), sm->id);
		sm++;
	}

	// The Scaler popup
	const PluginList &scalerPlugins = ScalerMan.getPlugins();
	_scalerPopUpDesc = new StaticTextWidget(boss, prefix + "grScalerPopupDesc", _("Scaler:"));
	_scalerPopUp = new PopUpWidget(boss, prefix + "grScalerPopup", Common::U32String(), kScalerPopUpCmd);

	_scalerPopUp->appendEntry(_("<default>"));
	_scalerPopUp->appendEntry(Common::U32String());
	for (uint scalerIndex = 0; scalerIndex < scalerPlugins.size(); scalerIndex++) {
		_scalerPopUp->appendEntry(_c(scalerPlugins[scalerIndex]->get<ScalerPluginObject>().getPrettyName(), context), scalerIndex);
	}

	_scaleFactorPopUp = new PopUpWidget(boss, prefix + "grScaleFactorPopup");
	updateScaleFactors(_scalerPopUp->getSelectedTag());

	if (g_system->hasFeature(OSystem::kFeatureShaders)) {
		if (g_system->getOverlayWidth() > 320)
			_shaderButton = new ButtonWidget(boss, prefix + "grShaderButton", _("Shader:"), _("Specifies path to the shader used for scaling the game screen"), kChooseShaderCmd);
		else
			_shaderButton = new ButtonWidget(boss, prefix + "grShaderButton", _c("Shader Path:", "lowres"), _("Specifies path to the shader used for scaling the game screen"), kChooseShaderCmd);
		_shader = new StaticTextWidget(boss, prefix + "grShader", _c("None", "shader"), _("Specifies path to the shader used for scaling the game screen"));

		_shaderClearButton = addClearButton(boss, prefix + "grShaderClearButton", kClearShaderCmd);
	}

	// Fullscreen checkbox
	_fullscreenCheckbox = new CheckboxWidget(boss, prefix + "grFullscreenCheckbox", _("Fullscreen mode"), Common::U32String(), kFullscreenToggled);

	_vsyncCheckbox = new CheckboxWidget(boss, prefix + "grVSyncCheckbox", _("V-Sync"), _("Wait for the vertical sync to refresh the screen in order to prevent tearing artifacts"));

	if (g_system->getOverlayWidth() > 320)
		_rendererTypePopUpDesc = new StaticTextWidget(boss, prefix + "grRendererTypePopupDesc", _("Game 3D Renderer:"));
	else
		_rendererTypePopUpDesc = new StaticTextWidget(boss, prefix + "grRendererTypePopupDesc", _c("Game 3D Renderer:", "lowres"));

	_rendererTypePopUp = new PopUpWidget(boss, prefix + "grRendererTypePopup");
	_rendererTypePopUp->appendEntry(_("<default>"), Graphics::kRendererTypeDefault);
	_rendererTypePopUp->appendEntry("");
	Common::Array<Graphics::RendererTypeDescription> rt = Graphics::Renderer::listTypes();
	for (Common::Array<Graphics::RendererTypeDescription>::iterator it = rt.begin();
	        it != rt.end(); ++it) {
		if (g_system->getOverlayWidth() > 320) {
			_rendererTypePopUp->appendEntry(_(it->description), it->id);
		} else {
			_rendererTypePopUp->appendEntry(_c(it->description, "lowres"), it->id);
		}
	}

	_antiAliasPopUpDesc = new StaticTextWidget(boss, prefix + "grAntiAliasPopupDesc", _("3D Anti-aliasing:"));
	_antiAliasPopUp = new PopUpWidget(boss, prefix + "grAntiAliasPopup");
	_antiAliasPopUp->appendEntry(_("<default>"), uint32(-1));
	_antiAliasPopUp->appendEntry("");
	_antiAliasPopUp->appendEntry(_("Disabled"), 0);
	const Common::Array<uint> levels = g_system->getSupportedAntiAliasingLevels();
	for (uint i = 0; i < levels.size(); i++) {
		_antiAliasPopUp->appendEntry(Common::String::format("%dx", levels[i]), levels[i]);
	}
	if (levels.empty()) {
		// Don't show the anti-aliasing selection menu when it is not supported
		_antiAliasPopUpDesc->setVisible(false);
		_antiAliasPopUp->setVisible(false);
	}

	// Filtering checkbox
	if (g_system->hasFeature(OSystem::kFeatureFilteringMode))
		_filteringCheckbox = new CheckboxWidget(boss, prefix + "grFilteringCheckbox", _("Filter graphics"), _("Use linear filtering when scaling graphics"));

	// Aspect ratio checkbox
	_aspectCheckbox = new CheckboxWidget(boss, prefix + "grAspectCheckbox", _("Aspect ratio correction"), _("Correct aspect ratio for games"));

	_enableGraphicSettings = true;
}

void OptionsDialog::addAudioControls(GuiObject *boss, const Common::String &prefix) {
	// The MIDI mode popup & a label
	if (g_system->getOverlayWidth() > 320)
		_midiPopUpDesc = new StaticTextWidget(boss, prefix + "auMidiPopupDesc", _domain == Common::ConfigManager::kApplicationDomain ? _("Preferred device:") : _("Music device:"), _domain == Common::ConfigManager::kApplicationDomain ? _("Specifies preferred sound device or sound card emulator") : _("Specifies output sound device or sound card emulator"));
	else
		_midiPopUpDesc = new StaticTextWidget(boss, prefix + "auMidiPopupDesc", _domain == Common::ConfigManager::kApplicationDomain ? _c("Preferred dev.:", "lowres") : _c("Music device:", "lowres"), _domain == Common::ConfigManager::kApplicationDomain ? _("Specifies preferred sound device or sound card emulator") : _("Specifies output sound device or sound card emulator"));
	_midiPopUp = new PopUpWidget(boss, prefix + "auMidiPopup", _("Specifies output sound device or sound card emulator"));

	// Populate it
	const Common::String allFlags = MidiDriver::musicType2GUIO((uint32)-1);
	bool hasMidiDefined = (strpbrk(_guioptions.c_str(), allFlags.c_str()) != nullptr);

	const PluginList p = MusicMan.getPlugins();
	for (PluginList::const_iterator m = p.begin(); m != p.end(); ++m) {
		MusicDevices i = (*m)->get<MusicPluginObject>().getDevices();
		for (MusicDevices::iterator d = i.begin(); d != i.end(); ++d) {
			Common::String deviceGuiOption = MidiDriver::musicType2GUIO(d->getMusicType());

			if ((_domain == Common::ConfigManager::kApplicationDomain && d->getMusicType() != MT_TOWNS  // global dialog - skip useless FM-Towns, C64, Amiga, AppleIIGS and SegaCD options there
				 && d->getMusicType() != MT_C64 && d->getMusicType() != MT_AMIGA && d->getMusicType() != MT_APPLEIIGS && d->getMusicType() != MT_PC98 && d->getMusicType() != MT_SEGACD)
				|| (_domain != Common::ConfigManager::kApplicationDomain && !hasMidiDefined) // No flags are specified
				|| (_guioptions.contains(deviceGuiOption)) // flag is present
				// HACK/FIXME: For now we have to show GM devices, even when the game only has GUIO_MIDIMT32 set,
				// else we would not show for example external devices connected via ALSA, since they are always
				// marked as General MIDI device.
				|| (deviceGuiOption.contains(GUIO_MIDIGM) && (_guioptions.contains(GUIO_MIDIMT32)))
				|| d->getMusicDriverId() == "auto" || d->getMusicDriverId() == "null") // always add default and null device
				_midiPopUp->appendEntry(_(d->getCompleteName()), d->getHandle());
		}
	}

	// The OPL emulator popup & a label
	_oplPopUpDesc = new StaticTextWidget(boss, prefix + "auOPLPopupDesc", _("AdLib emulator:"), _("AdLib is used for music in many games"));
	if (ConfMan.isKeyTemporary("opl_driver"))
		_oplPopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorOverride);
	_oplPopUp = new PopUpWidget(boss, prefix + "auOPLPopup", _("AdLib is used for music in many games"));

	// Populate it
	const OPL::Config::EmulatorDescription *ed = OPL::Config::getAvailable();
	while (ed->name) {
		_oplPopUp->appendEntry(_(ed->description), ed->id);
		++ed;
	}

	_enableAudioSettings = true;
}

void OptionsDialog::addMIDIControls(GuiObject *boss, const Common::String &prefix) {
	_gmDevicePopUpDesc = new StaticTextWidget(boss, prefix + "auPrefGmPopupDesc", _("GM device:"), _("Specifies default sound device for General MIDI output"));
	_gmDevicePopUp = new PopUpWidget(boss, prefix + "auPrefGmPopup");

	// Populate
	const PluginList p = MusicMan.getPlugins();
	// Make sure the null device is the first one in the list to avoid undesired
	// auto detection for users who don't have a saved setting yet.
	for (PluginList::const_iterator m = p.begin(); m != p.end(); ++m) {
		MusicDevices i = (*m)->get<MusicPluginObject>().getDevices();
		for (MusicDevices::iterator d = i.begin(); d != i.end(); ++d) {
			if (d->getMusicDriverId() == "null")
				_gmDevicePopUp->appendEntry(_("Don't use General MIDI music"), d->getHandle());
		}
	}
	// Now we add the other devices.
	for (PluginList::const_iterator m = p.begin(); m != p.end(); ++m) {
		MusicDevices i = (*m)->get<MusicPluginObject>().getDevices();
		for (MusicDevices::iterator d = i.begin(); d != i.end(); ++d) {
			if (d->getMusicType() >= MT_GM) {
				if (d->getMusicType() != MT_MT32)
					_gmDevicePopUp->appendEntry(d->getCompleteName(), d->getHandle());
			} else if (d->getMusicDriverId() == "auto") {
				_gmDevicePopUp->appendEntry(_("Use first available device"), d->getHandle());
			}
		}
	}

	if (!_domain.equals(Common::ConfigManager::kApplicationDomain)) {
		_gmDevicePopUpDesc->setEnabled(false);
		_gmDevicePopUp->setEnabled(false);
	}

	// SoundFont
	if (g_system->getOverlayWidth() > 320)
		_soundFontButton = new ButtonWidget(boss, prefix + "mcFontButton", _("SoundFont:"), _("SoundFont is supported by some audio cards, FluidSynth and Timidity"), kChooseSoundFontCmd);
	else
		_soundFontButton = new ButtonWidget(boss, prefix + "mcFontButton", _c("SoundFont:", "lowres"), _("SoundFont is supported by some audio cards, FluidSynth and Timidity"), kChooseSoundFontCmd);
	_soundFont = new StaticTextWidget(boss, prefix + "mcFontPath", _c("None", "soundfont"), _("SoundFont is supported by some audio cards, FluidSynth and Timidity"));

	_soundFontClearButton = addClearButton(boss, prefix + "mcFontClearButton", kClearSoundFontCmd);

	// Multi midi setting
	_multiMidiCheckbox = new CheckboxWidget(boss, prefix + "mcMixedCheckbox", _("Mixed AdLib/MIDI mode"), _("Use both MIDI and AdLib sound generation"));

	// MIDI gain setting (FluidSynth uses this)
	_midiGainDesc = new StaticTextWidget(boss, prefix + "mcMidiGainText", _("MIDI gain:"));
	_midiGainSlider = new SliderWidget(boss, prefix + "mcMidiGainSlider", Common::U32String(), kMidiGainChanged);
	_midiGainSlider->setMinValue(0);
	_midiGainSlider->setMaxValue(1000);
	_midiGainLabel = new StaticTextWidget(boss, prefix + "mcMidiGainLabel", Common::U32String("1.00"));

	_enableMIDISettings = true;
}

void OptionsDialog::addMT32Controls(GuiObject *boss, const Common::String &prefix) {
	_mt32DevicePopUpDesc = new StaticTextWidget(boss, prefix + "auPrefMt32PopupDesc", _("MT-32 Device:"), _("Specifies default sound device for Roland MT-32/LAPC1/CM32l/CM64 output"));
	_mt32DevicePopUp = new PopUpWidget(boss, prefix + "auPrefMt32Popup");

	// Native mt32 setting
	if (g_system->getOverlayWidth() > 320)
		_mt32Checkbox = new CheckboxWidget(boss, prefix + "mcMt32Checkbox", _("True Roland MT-32 (disable GM emulation)"), _("Check if you want to use your real hardware Roland-compatible sound device connected to your computer"));
	else
		_mt32Checkbox = new CheckboxWidget(boss, prefix + "mcMt32Checkbox", _c("True Roland MT-32 (no GM emulation)", "lowres"), _("Check if you want to use your real hardware Roland-compatible sound device connected to your computer"));

	// GS Extensions setting
	_enableGSCheckbox = new CheckboxWidget(boss, prefix + "mcGSCheckbox", _("Roland GS device (enable MT-32 mappings)"), _("Check if you want to enable patch mappings to emulate an MT-32 on a Roland GS device"));

	const PluginList p = MusicMan.getPlugins();
	// Make sure the null device is the first one in the list to avoid undesired
	// auto detection for users who don't have a saved setting yet.
	for (PluginList::const_iterator m = p.begin(); m != p.end(); ++m) {
		MusicDevices i = (*m)->get<MusicPluginObject>().getDevices();
		for (MusicDevices::iterator d = i.begin(); d != i.end(); ++d) {
			if (d->getMusicDriverId() == "null")
				_mt32DevicePopUp->appendEntry(_("Don't use Roland MT-32 music"), d->getHandle());
		}
	}
	// Now we add the other devices.
	for (PluginList::const_iterator m = p.begin(); m != p.end(); ++m) {
		MusicDevices i = (*m)->get<MusicPluginObject>().getDevices();
		for (MusicDevices::iterator d = i.begin(); d != i.end(); ++d) {
			if (d->getMusicType() >= MT_GM)
				_mt32DevicePopUp->appendEntry(d->getCompleteName(), d->getHandle());
			else if (d->getMusicDriverId() == "auto")
				_mt32DevicePopUp->appendEntry(_("Use first available device"), d->getHandle());
		}
	}

	if (!_domain.equals(Common::ConfigManager::kApplicationDomain)) {
		_mt32DevicePopUpDesc->setEnabled(false);
		_mt32DevicePopUp->setEnabled(false);
	}

	_enableMT32Settings = true;
}

// The function has an extra slider range parameter, since both the launcher and SCUMM engine
// make use of the widgets. The launcher range is 0-255. SCUMM's 0-9
void OptionsDialog::addSubtitleControls(GuiObject *boss, const Common::String &prefix, int maxSliderVal) {

	if (g_system->getOverlayWidth() > 320) {
		_subToggleDesc = new StaticTextWidget(boss, prefix + "subToggleDesc", _("Text and speech:"));

		_subToggleGroup = new RadiobuttonGroup(boss, kSubtitleToggle);

		_subToggleSpeechOnly = new RadiobuttonWidget(boss, prefix + "subToggleSpeechOnly", _subToggleGroup, kSubtitlesSpeech, _("Speech"));
		_subToggleSubOnly = new RadiobuttonWidget(boss, prefix + "subToggleSubOnly", _subToggleGroup, kSubtitlesSubs, _("Subtitles"));
		_subToggleSubBoth = new RadiobuttonWidget(boss, prefix + "subToggleSubBoth", _subToggleGroup, kSubtitlesBoth, _("Both"));

		_subSpeedDesc = new StaticTextWidget(boss, prefix + "subSubtitleSpeedDesc", _("Subtitle speed:"));
	} else {
		_subToggleDesc = new StaticTextWidget(boss, prefix + "subToggleDesc", _c("Text and speech:", "lowres"));

		_subToggleGroup = new RadiobuttonGroup(boss, kSubtitleToggle);

		_subToggleSpeechOnly = new RadiobuttonWidget(boss, prefix + "subToggleSpeechOnly", _subToggleGroup, kSubtitlesSpeech, _("Spch"), _("Speech"));
		_subToggleSubOnly = new RadiobuttonWidget(boss, prefix + "subToggleSubOnly", _subToggleGroup, kSubtitlesSubs, _("Subs"), _("Subtitles"));
		_subToggleSubBoth = new RadiobuttonWidget(boss, prefix + "subToggleSubBoth", _subToggleGroup, kSubtitlesBoth, _c("Both", "lowres"), _("Show subtitles and play speech"));

		_subSpeedDesc = new StaticTextWidget(boss, prefix + "subSubtitleSpeedDesc", _c("Subtitle speed:", "lowres"));
	}

	if (ConfMan.isKeyTemporary("talkspeed"))
		_subSpeedDesc->setFontColor(ThemeEngine::FontColor::kFontColorOverride);
	if (ConfMan.isKeyTemporary("subtitles"))
		_subToggleDesc->setFontColor(ThemeEngine::FontColor::kFontColorOverride);

	// Subtitle speed
	_subSpeedSlider = new SliderWidget(boss, prefix + "subSubtitleSpeedSlider", Common::U32String(), kSubtitleSpeedChanged);
	_subSpeedLabel = new StaticTextWidget(boss, prefix + "subSubtitleSpeedLabel", Common::U32String("100%"), Common::U32String(), ThemeEngine::kFontStyleBold, Common::UNK_LANG, false);
	_subSpeedSlider->setMinValue(0); _subSpeedSlider->setMaxValue(maxSliderVal);
	_subSpeedLabel->setFlags(WIDGET_CLEARBG);

	_enableSubtitleSettings = true;
	_enableSubtitleToggle = true;
}

void OptionsDialog::addVolumeControls(GuiObject *boss, const Common::String &prefix) {

	// Volume controllers
	if (g_system->getOverlayWidth() > 320)
		_musicVolumeDesc = new StaticTextWidget(boss, prefix + "vcMusicText", _("Music volume:"));
	else
		_musicVolumeDesc = new StaticTextWidget(boss, prefix + "vcMusicText", _c("Music volume:", "lowres"));
	if (ConfMan.isKeyTemporary("music_volume"))
		_musicVolumeDesc->setFontColor(ThemeEngine::FontColor::kFontColorOverride);
	_musicVolumeSlider = new SliderWidget(boss, prefix + "vcMusicSlider", Common::U32String(), kMusicVolumeChanged);
	_musicVolumeLabel = new StaticTextWidget(boss, prefix + "vcMusicLabel", Common::U32String("100%"), Common::U32String(), ThemeEngine::kFontStyleBold, Common::UNK_LANG, false);
	_musicVolumeSlider->setMinValue(0);
	_musicVolumeSlider->setMaxValue(Audio::Mixer::kMaxMixerVolume);
	_musicVolumeLabel->setFlags(WIDGET_CLEARBG);

	_muteCheckbox = new CheckboxWidget(boss, prefix + "vcMuteCheckbox", _("Mute all"), Common::U32String(), kMuteAllChanged);

	if (g_system->getOverlayWidth() > 320)
		_sfxVolumeDesc = new StaticTextWidget(boss, prefix + "vcSfxText", _("SFX volume:"), _("Special sound effects volume"));
	else
		_sfxVolumeDesc = new StaticTextWidget(boss, prefix + "vcSfxText", _c("SFX volume:", "lowres"), _("Special sound effects volume"));
	if (ConfMan.isKeyTemporary("sfx_volume"))
		_sfxVolumeDesc->setFontColor(ThemeEngine::FontColor::kFontColorOverride);
	_sfxVolumeSlider = new SliderWidget(boss, prefix + "vcSfxSlider", _("Special sound effects volume"), kSfxVolumeChanged);
	_sfxVolumeLabel = new StaticTextWidget(boss, prefix + "vcSfxLabel", Common::U32String("100%"), Common::U32String(), ThemeEngine::kFontStyleBold, Common::UNK_LANG, false);
	_sfxVolumeSlider->setMinValue(0);
	_sfxVolumeSlider->setMaxValue(Audio::Mixer::kMaxMixerVolume);
	_sfxVolumeLabel->setFlags(WIDGET_CLEARBG);

	if (g_system->getOverlayWidth() > 320)
		_speechVolumeDesc = new StaticTextWidget(boss, prefix + "vcSpeechText" , _("Speech volume:"));
	else
		_speechVolumeDesc = new StaticTextWidget(boss, prefix + "vcSpeechText" , _c("Speech volume:", "lowres"));
	if (ConfMan.isKeyTemporary("speech_volume"))
		_speechVolumeDesc->setFontColor(ThemeEngine::FontColor::kFontColorOverride);
	_speechVolumeSlider = new SliderWidget(boss, prefix + "vcSpeechSlider", Common::U32String(), kSpeechVolumeChanged);
	_speechVolumeLabel = new StaticTextWidget(boss, prefix + "vcSpeechLabel", Common::U32String("100%"), Common::U32String(), ThemeEngine::kFontStyleBold, Common::UNK_LANG, false);
	_speechVolumeSlider->setMinValue(0);
	_speechVolumeSlider->setMaxValue(Audio::Mixer::kMaxMixerVolume);
	_speechVolumeLabel->setFlags(WIDGET_CLEARBG);

	_enableVolumeSettings = true;
}

bool OptionsDialog::loadMusicDeviceSetting(PopUpWidget *popup, Common::String setting, MusicType preferredType) {
	if (!popup || !popup->isEnabled())
		return true;

	if (_domain != Common::ConfigManager::kApplicationDomain || ConfMan.hasKey(setting, _domain) || preferredType) {
		const Common::String drv = ConfMan.get(setting, (_domain != Common::ConfigManager::kApplicationDomain && !ConfMan.hasKey(setting, _domain)) ? Common::ConfigManager::kApplicationDomain : _domain);
		const PluginList p = MusicMan.getPlugins();

		for (PluginList::const_iterator m = p.begin(); m != p.end(); ++m) {
			MusicDevices i = (*m)->get<MusicPluginObject>().getDevices();
			for (MusicDevices::iterator d = i.begin(); d != i.end(); ++d) {
				if (setting.empty() ? (preferredType == d->getMusicType()) : (drv == d->getCompleteId())) {
					popup->setSelectedTag(d->getHandle());
					return popup->getSelected() != -1;
				}
			}
		}
	}

	return false;
}

void OptionsDialog::saveMusicDeviceSetting(PopUpWidget *popup, Common::String setting) {
	if (!popup || !_enableAudioSettings)
		return;

	const PluginList p = MusicMan.getPlugins();
	bool found = false;
	for (PluginList::const_iterator m = p.begin(); m != p.end() && !found; ++m) {
		MusicDevices i = (*m)->get<MusicPluginObject>().getDevices();
		for (MusicDevices::iterator d = i.begin(); d != i.end(); ++d) {
			if (d->getHandle() == popup->getSelectedTag()) {
				ConfMan.set(setting, d->getCompleteId(), _domain);
				found = true;
				break;
			}
		}
	}

	if (!found)
		ConfMan.removeKey(setting, _domain);
}

int OptionsDialog::getSubtitleMode(bool subtitles, bool speech_mute) {
	if (_guioptions.contains(GUIO_NOSUBTITLES))
		return kSubtitlesSpeech; // Speech only
	if (_guioptions.contains(GUIO_NOSPEECH))
		return kSubtitlesSubs; // Subtitles only

	if (!subtitles && !speech_mute) // Speech only
		return kSubtitlesSpeech;
	else if (subtitles && !speech_mute) // Speech and subtitles
		return kSubtitlesBoth;
	else if (subtitles && speech_mute) // Subtitles only
		return kSubtitlesSubs;
	else
		warning("Wrong configuration: Both subtitles and speech are off. Assuming subtitles only");
	return kSubtitlesSubs;
}

void OptionsDialog::updateMusicVolume(const int newValue) const {
	_musicVolumeLabel->setValue(newValue);
	_musicVolumeSlider->setValue(newValue);
	_musicVolumeLabel->markAsDirty();
	_musicVolumeSlider->markAsDirty();
}

void OptionsDialog::updateSfxVolume(const int newValue) const {
	_sfxVolumeLabel->setValue(newValue);
	_sfxVolumeSlider->setValue(newValue);
	_sfxVolumeLabel->markAsDirty();
	_sfxVolumeSlider->markAsDirty();
}

void OptionsDialog::updateSpeechVolume(const int newValue) const {
	_speechVolumeLabel->setValue(newValue);
	_speechVolumeSlider->setValue(newValue);
	_speechVolumeLabel->markAsDirty();
	_speechVolumeSlider->markAsDirty();
}

void OptionsDialog::reflowLayout() {
	if (_graphicsTabId != -1 && _tabWidget)
		_tabWidget->setTabTitle(_graphicsTabId, g_system->getOverlayWidth() > 320 ? _("Graphics") : _("GFX"));

	Dialog::reflowLayout();
	setupGraphicsTab();
}

void OptionsDialog::setupGraphicsTab() {
	if (_graphicsTabId != -1) {
		// Since we do not create shader controls, the rebuild is required
		// Fixes crash when switching from SDL Surface to OpenGL
		if (!_shader && g_system->hasFeature(OSystem::kFeatureShaders)) {
			rebuild();
		}
		setGraphicSettingsState(_enableGraphicSettings);
	}
	if (!_fullscreenCheckbox)
		return;
	_gfxPopUpDesc->setVisible(true);
	_gfxPopUp->setVisible(true);
	if (g_system->hasFeature(OSystem::kFeatureStretchMode)) {
		_stretchPopUpDesc->setVisible(true);
		_stretchPopUp->setVisible(true);
	} else {
		_stretchPopUpDesc->setVisible(false);
		_stretchPopUp->setVisible(false);
	}
	_fullscreenCheckbox->setVisible(true);
	if (g_system->hasFeature(OSystem::kFeatureFilteringMode))
		_filteringCheckbox->setVisible(true);

	_aspectCheckbox->setVisible(true);
	_renderModePopUpDesc->setVisible(true);
	_renderModePopUp->setVisible(true);

	if (g_system->hasFeature(OSystem::kFeatureScalers)) {
		_scalerPopUpDesc->setVisible(true);
		if (ConfMan.isKeyTemporary("scaler") || ConfMan.isKeyTemporary("scale_factor"))
			_scalerPopUpDesc->setFontColor(ThemeEngine::FontColor::kFontColorOverride);
		_scalerPopUp->setVisible(true);
		_scaleFactorPopUp->setVisible(true);
	} else {
		_scalerPopUpDesc->setVisible(false);
		_scalerPopUp->setVisible(false);
		_scaleFactorPopUp->setVisible(false);
	}

	if (g_system->hasFeature(OSystem::kFeatureShaders)) {
		_shaderButton->setVisible(true);
		_shader->setVisible(true);
		_shaderClearButton->setVisible(true);
	}
}

void OptionsDialog::updateScaleFactors(uint32 tag) {
	if ((int32)tag >= 0) {
		const PluginList &scalerPlugins = ScalerMan.getPlugins();
		const Common::Array<uint> &factors = scalerPlugins[tag]->get<ScalerPluginObject>().getFactors();

		_scaleFactorPopUp->clearEntries();
		for (Common::Array<uint>::const_iterator it = factors.begin(); it != factors.end(); it++) {
			_scaleFactorPopUp->appendEntry(Common::U32String::format("%dx", (*it)), (*it));
		}

		if (g_system->getScaler() == tag) {
			_scaleFactorPopUp->setSelectedTag(g_system->getScaleFactor());
		} else {
			_scaleFactorPopUp->setSelectedTag(scalerPlugins[tag]->get<ScalerPluginObject>().getDefaultFactor());
		}
	} else {
		_scaleFactorPopUp->clearEntries();
		_scaleFactorPopUp->appendEntry(_("<default>"));
		_scaleFactorPopUp->setSelected(0);
	}
}

#pragma mark -


GlobalOptionsDialog::GlobalOptionsDialog(LauncherDialog *launcher)
	: OptionsDialog(Common::ConfigManager::kApplicationDomain, "GlobalOptions"), CommandSender(nullptr), _launcher(launcher) {
#ifdef USE_FLUIDSYNTH
	_fluidSynthSettingsDialog = nullptr;
#endif
	_savePath = nullptr;
	_savePathClearButton = nullptr;
	_themePath = nullptr;
	_themePathClearButton = nullptr;
	_iconPath = nullptr;
	_iconPathClearButton = nullptr;
	_extraPath = nullptr;
	_extraPathClearButton = nullptr;
#ifdef DYNAMIC_MODULES
	_pluginsPath = nullptr;
	_pluginsPathClearButton = nullptr;
#endif
	_browserPath = nullptr;
	_browserPathClearButton = nullptr;
	_curTheme = nullptr;
	_guiBasePopUpDesc = nullptr;
	_guiBasePopUp = nullptr;
	_rendererPopUpDesc = nullptr;
	_rendererPopUp = nullptr;
	_autosavePeriodPopUpDesc = nullptr;
	_autosavePeriodPopUp = nullptr;
	_guiLanguagePopUpDesc = nullptr;
	_guiLanguagePopUp = nullptr;
	_guiLanguageUseGameLanguageCheckbox = nullptr;
	_useSystemDialogsCheckbox = nullptr;
	_guiReturnToLauncherAtExit = nullptr;
	_guiConfirmExit = nullptr;
#ifdef USE_UPDATES
	_updatesPopUpDesc = nullptr;
	_updatesPopUp = nullptr;
#endif
#ifdef USE_CLOUD
#ifdef USE_LIBCURL
	_selectedStorageIndex = CloudMan.getStorageIndex();
	_storagePopUpDesc = nullptr;
	_storagePopUp = nullptr;
	_storageDisabledHint = nullptr;
	_storageEnableButton = nullptr;
	_storageUsernameDesc = nullptr;
	_storageUsername = nullptr;
	_storageUsedSpaceDesc = nullptr;
	_storageUsedSpace = nullptr;
	_storageSyncHint = nullptr;
	_storageLastSyncDesc = nullptr;
	_storageLastSync = nullptr;
	_storageSyncSavesButton = nullptr;
	_storageDownloadHint = nullptr;
	_storageDownloadButton = nullptr;
	_storageDisconnectHint = nullptr;
	_storageDisconnectButton = nullptr;

	_connectingStorage = false;
	_storageWizardNotConnectedHint = nullptr;
	_storageWizardOpenLinkHint = nullptr;
	_storageWizardLink = nullptr;
	_storageWizardCodeHint = nullptr;
	_storageWizardCodeBox = nullptr;
	_storageWizardPasteButton = nullptr;
	_storageWizardConnectButton = nullptr;
	_storageWizardConnectionStatusHint = nullptr;
	_redrawCloudTab = false;
#endif
#ifdef USE_SDL_NET
	_runServerButton = nullptr;
	_serverInfoLabel = nullptr;
	_rootPathButton = nullptr;
	_rootPath = nullptr;
	_rootPathClearButton = nullptr;
	_serverPortDesc = nullptr;
	_serverPort = nullptr;
	_serverPortClearButton = nullptr;
	_featureDescriptionLine1 = nullptr;
	_featureDescriptionLine2 = nullptr;
	_serverWasRunning = false;
#endif
#endif
#ifdef USE_TTS
	_enableTTS = false;
	_ttsCheckbox = nullptr;
	_ttsVoiceSelectionPopUp = nullptr;
#endif
#ifdef USE_DISCORD
	_discordRpcCheckbox = nullptr;
#endif
}

GlobalOptionsDialog::~GlobalOptionsDialog() {
#ifdef USE_FLUIDSYNTH
	delete _fluidSynthSettingsDialog;
#endif
}

void GlobalOptionsDialog::build() {
	// The tab widget
	TabWidget *tab = new TabWidget(this, "GlobalOptions.TabWidget");

	//
	// 1) The graphics tab
	//
	_graphicsTabId = tab->addTab(g_system->getOverlayWidth() > 320 ? _("Graphics") : _("GFX"), "GlobalOptions_Graphics", false);
	ScrollContainerWidget *graphicsContainer = new ScrollContainerWidget(tab, "GlobalOptions_Graphics.Container", "GlobalOptions_Graphics_Container", kGraphicsTabContainerReflowCmd);
	graphicsContainer->setTarget(this);
	graphicsContainer->setBackgroundType(ThemeEngine::kWidgetBackgroundNo);
	addGraphicControls(graphicsContainer, "GlobalOptions_Graphics_Container.");

	//
	// The control tab (currently visible only for SDL and Vita platform, visibility checking by features
	//
	if (g_system->hasFeature(OSystem::kFeatureTouchpadMode) ||
		g_system->hasFeature(OSystem::kFeatureKbdMouseSpeed) ||
		g_system->hasFeature(OSystem::kFeatureJoystickDeadzone)) {
		tab->addTab(_("Control"), "GlobalOptions_Control");
		addControlControls(tab, "GlobalOptions_Control.");
	}

	//
	// The Keymap tab
	//
	Common::KeymapArray keymaps;

	Common::Keymap *primaryGlobalKeymap = g_system->getEventManager()->getGlobalKeymap();
	if (primaryGlobalKeymap && !primaryGlobalKeymap->getActions().empty()) {
		keymaps.push_back(primaryGlobalKeymap);
	}

	keymaps.push_back(g_system->getGlobalKeymaps());

	Common::Keymap *guiKeymap = g_gui.getKeymap();
	if (guiKeymap && !guiKeymap->getActions().empty()) {
		keymaps.push_back(guiKeymap);
	}

	if (!keymaps.empty()) {
		tab->addTab(_("Keymaps"), "GlobalOptions_KeyMapper", false);
		addKeyMapperControls(tab, "GlobalOptions_KeyMapper.", keymaps, Common::ConfigManager::kKeymapperDomain);
	}

	//
	// The backend tab (shown only if the backend implements one)
	//
	int backendTabId = tab->addTab(_("Backend"), "GlobalOptions_Backend", false);

	g_system->registerDefaultSettings(_domain);
	_backendOptions = g_system->buildBackendOptionsWidget(tab, "GlobalOptions_Backend.Container", _domain);

	if (_backendOptions) {
		_backendOptions->setParentDialog(this);
	} else {
		tab->removeTab(backendTabId);
	}

	//
	// 2) The audio tab
	//
	tab->addTab(_("Audio"), "GlobalOptions_Audio");
	addAudioControls(tab, "GlobalOptions_Audio.");
	addSubtitleControls(tab, "GlobalOptions_Audio.");

	if (g_system->getOverlayWidth() > 320)
		tab->addTab(_("Volume"), "GlobalOptions_Volume");
	else
		tab->addTab(_c("Volume", "lowres"), "GlobalOptions_Volume");
	addVolumeControls(tab, "GlobalOptions_Volume.");

	// TODO: cd drive setting

	//
	// 3) The MIDI tab
	//
	_midiTabId = tab->addTab(_("MIDI"), "GlobalOptions_MIDI");
	addMIDIControls(tab, "GlobalOptions_MIDI.");

	//
	// 4) The MT-32 tab
	//
	tab->addTab(_("MT-32"), "GlobalOptions_MT32");
	addMT32Controls(tab, "GlobalOptions_MT32.");

	//
	// 5) The Paths tab
	//
	if (g_system->getOverlayWidth() > 320)
		_pathsTabId = tab->addTab(_("Paths"), "GlobalOptions_Paths");
	else
		_pathsTabId = tab->addTab(_c("Paths", "lowres"), "GlobalOptions_Paths");
	addPathsControls(tab, "GlobalOptions_Paths.", g_system->getOverlayWidth() <= 320);

	//
	// 6) The miscellaneous tab
	//
	if (g_system->getOverlayWidth() > 320)
		tab->addTab(_("Misc"), "GlobalOptions_Misc", false);
	else
		tab->addTab(_c("Misc", "lowres"), "GlobalOptions_Misc", false);
	ScrollContainerWidget *miscContainer = new ScrollContainerWidget(tab, "GlobalOptions_Misc.Container", "GlobalOptions_Misc_Container");
	miscContainer->setTarget(this);
	miscContainer->setBackgroundType(ThemeEngine::kWidgetBackgroundNo);
	addMiscControls(miscContainer, "GlobalOptions_Misc_Container.", g_system->getOverlayWidth() <= 320);

#ifdef USE_CLOUD
#ifdef USE_LIBCURL
	//
	// 7) The Cloud tab (remote storages)
	//
	if (g_system->getOverlayWidth() > 320)
		tab->addTab(_("Cloud"), "GlobalOptions_Cloud", false);
	else
		tab->addTab(_c("Cloud", "lowres"), "GlobalOptions_Cloud", false);

	ScrollContainerWidget *container = new ScrollContainerWidget(tab, "GlobalOptions_Cloud.Container", "GlobalOptions_Cloud_Container", kCloudTabContainerReflowCmd);
	container->setTarget(this);
	container->setBackgroundType(ThemeEngine::kWidgetBackgroundNo);
	setTarget(container);

	addCloudControls(container, "GlobalOptions_Cloud_Container.", g_system->getOverlayWidth() <= 320);
#endif // USE_LIBCURL
#ifdef USE_SDL_NET
	//
	// 8) The LAN tab (local "cloud" webserver)
	//
	if (g_system->getOverlayWidth() > 320)
		tab->addTab(_("LAN"), "GlobalOptions_Network");
	else
		tab->addTab(_c("LAN", "lowres"), "GlobalOptions_Network");
	addNetworkControls(tab, "GlobalOptions_Network.", g_system->getOverlayWidth() <= 320);
#endif // USE_SDL_NET
#endif // USE_CLOUD

	//Accessibility
#ifdef USE_TTS
	if (g_system->getOverlayWidth() > 320)
		tab->addTab(_("Accessibility"), "GlobalOptions_Accessibility");
	else
		tab->addTab(_c("Accessibility", "lowres"), "GlobalOptions_Accessibility");
	addAccessibilityControls(tab, "GlobalOptions_Accessibility.");
#endif // USE_TTS

	// Activate the first tab
	tab->setActiveTab(0);
	_tabWidget = tab;

	// Add OK & Cancel buttons
	new ButtonWidget(this, "GlobalOptions.Cancel", _("Cancel"), _("Discard changes and close the dialog"), kCloseCmd);
	new ButtonWidget(this, "GlobalOptions.Apply", _("Apply"), _("Apply changes without closing the dialog"), kApplyCmd);
	new ButtonWidget(this, "GlobalOptions.Ok", _("OK"), _("Apply changes and close the dialog"), kOKCmd);

#ifdef USE_FLUIDSYNTH
	_fluidSynthSettingsDialog = new FluidSynthSettingsDialog();
#endif

	OptionsDialog::build();

#if !defined(__DC__)
	const auto setPath =
	[&](GUI::StaticTextWidget *const widget, const Common::String &pathType, const Common::U32String &defaultLabel) {
		Common::String path(ConfMan.get(pathType));
		if (ConfMan.isKeyTemporary(pathType)) {
			widget->setFontColor(ThemeEngine::FontColor::kFontColorOverride);
		}
		if (path.empty() || !ConfMan.hasKey(pathType, _domain)) {
			widget->setLabel(defaultLabel);
		} else {
			widget->setLabel(path);
		}
	};

	setPath(_savePath, "savepath", _("Default"));
	setPath(_themePath, "themepath", _c("None", "path"));
	setPath(_iconPath, "iconspath", _("Default"));
	setPath(_extraPath, "extrapath", _c("None", "path"));

#ifdef DYNAMIC_MODULES
	Common::String pluginsPath(ConfMan.get("pluginspath", _domain));
	if (pluginsPath.empty() || !ConfMan.hasKey("pluginspath", _domain)) {
		_pluginsPath->setLabel(_c("None", "path"));
	} else {
		_pluginsPath->setLabel(pluginsPath);
	}
#endif
#endif

	// Misc Tab
	_guiBasePopUp->setSelected(2);
	int value = ConfMan.getInt("gui_scale");
	for (int i = 0; guiBaseLabels[i]; i++) {
		if (value == guiBaseValues[i])
			_guiBasePopUp->setSelected(i);
	}

	_autosavePeriodPopUp->setSelected(1);
	value = ConfMan.getInt("autosave_period");
	for (int i = 0; savePeriodLabels[i]; i++) {
		if (value == savePeriodValues[i])
			_autosavePeriodPopUp->setSelected(i);
	}

	ThemeEngine::GraphicsMode mode = ThemeEngine::findMode(ConfMan.get("gui_renderer"));
	if (mode == ThemeEngine::kGfxDisabled)
		mode = ThemeEngine::_defaultRendererMode;
	_rendererPopUp->setSelectedTag(mode);

#ifdef USE_CLOUD
#ifdef USE_SDL_NET
	Common::String rootPath(ConfMan.get("rootpath", "cloud"));
	if (rootPath.empty() || !ConfMan.hasKey("rootpath", "cloud")) {
		_rootPath->setLabel(_c("None", "path"));
	} else {
		_rootPath->setLabel(rootPath);
	}
#endif
#endif
}

void GlobalOptionsDialog::clean() {
#ifdef USE_FLUIDSYNTH
	delete _fluidSynthSettingsDialog;
	_fluidSynthSettingsDialog = nullptr;
#endif

	OptionsDialog::clean();
}

void GlobalOptionsDialog::addMIDIControls(GuiObject *boss, const Common::String &prefix) {
	OptionsDialog::addMIDIControls(boss, prefix);

#ifdef USE_FLUIDSYNTH
	new ButtonWidget(boss, prefix + "mcFluidSynthSettings", _("FluidSynth Settings"), Common::U32String(), kFluidSynthSettingsCmd);
#endif
}

void GlobalOptionsDialog::addPathsControls(GuiObject *boss, const Common::String &prefix, bool lowres) {
#if !defined(__DC__)
	// These two buttons have to be extra wide, or the text will be
	// truncated in the small version of the GUI.

	// Save game path
	if (!lowres)
		new ButtonWidget(boss, prefix + "SaveButton", _("Save Path:"), _("Specifies where your saved games are put"), kChooseSaveDirCmd);
	else
		new ButtonWidget(boss, prefix + "SaveButton", _c("Save Path:", "lowres"), _("Specifies where your saved games are put"), kChooseSaveDirCmd);
	_savePath = new StaticTextWidget(boss, prefix + "SavePath", Common::U32String("/foo/bar"), _("Specifies where your saved games are put. A red coloring indicates the value is temporary and will not get saved"));

	_savePathClearButton = addClearButton(boss, prefix + "SavePathClearButton", kSavePathClearCmd);

	if (!lowres)
		new ButtonWidget(boss, prefix + "ThemeButton", _("Theme Path:"), Common::U32String(), kChooseThemeDirCmd);
	else
		new ButtonWidget(boss, prefix + "ThemeButton", _c("Theme Path:", "lowres"), Common::U32String(), kChooseThemeDirCmd);
	_themePath = new StaticTextWidget(boss, prefix + "ThemePath", _c("None", "path"));

	_themePathClearButton = addClearButton(boss, prefix + "ThemePathClearButton", kThemePathClearCmd);

	if (!lowres)
		new ButtonWidget(boss, prefix + "IconButton", _("Icon Path:"), Common::U32String(), kChooseIconDirCmd);
	else
		new ButtonWidget(boss, prefix + "IconButton", _c("Icon Path:", "lowres"), Common::U32String(), kChooseIconDirCmd);
	_iconPath = new StaticTextWidget(boss, prefix + "IconPath", _c("Default", "path"));

	_iconPathClearButton = addClearButton(boss, prefix + "IconPathClearButton", kIconPathClearCmd);

	if (!lowres)
		new ButtonWidget(boss, prefix + "ExtraButton", _("Extra Path:"), _("Specifies path to additional data used by all games or ScummVM"), kChooseExtraDirCmd);
	else
		new ButtonWidget(boss, prefix + "ExtraButton", _c("Extra Path:", "lowres"), _("Specifies path to additional data used by all games or ScummVM"), kChooseExtraDirCmd);
	_extraPath = new StaticTextWidget(boss, prefix + "ExtraPath", _c("None", "path"), _("Specifies path to additional data used by all games or ScummVM"));

	_extraPathClearButton = addClearButton(boss, prefix + "ExtraPathClearButton", kExtraPathClearCmd);

#ifdef DYNAMIC_MODULES
	if (!lowres)
		new ButtonWidget(boss, prefix + "PluginsButton", _("Plugins Path:"), Common::U32String(), kChoosePluginsDirCmd);
	else
		new ButtonWidget(boss, prefix + "PluginsButton", _c("Plugins Path:", "lowres"), Common::U32String(), kChoosePluginsDirCmd);
	_pluginsPath = new StaticTextWidget(boss, prefix + "PluginsPath", _c("None", "path"));

	_pluginsPathClearButton = addClearButton(boss, "GlobalOptions_Paths.PluginsPathClearButton", kPluginsPathClearCmd);
#endif // DYNAMIC_MODULES
#endif // !defined(__DC__)

	Common::U32String confPath = ConfMan.getCustomConfigFileName();
	if (confPath.empty())
		confPath = g_system->getDefaultConfigFileName();
	StaticTextWidget* configPathWidget = new StaticTextWidget(boss, prefix + "ConfigPath", _("ScummVM config path: ") + confPath, confPath);
	if (ConfMan.isKeyTemporary("config"))
		configPathWidget->setFontColor(ThemeEngine::FontColor::kFontColorOverride);


	Common::U32String browserPath = _("<default>");
	if (ConfMan.hasKey("browser_lastpath"))
		browserPath = ConfMan.get("browser_lastpath");

	// I18N: Referring to the last path memorized when adding a game
	_browserPath = new StaticTextWidget(boss, prefix + "BrowserPath", _("Last browser path: ") + browserPath, browserPath);
	_browserPathClearButton = addClearButton(boss, prefix + "BrowserPathClearButton", kBrowserPathClearCmd);
}

void GlobalOptionsDialog::addMiscControls(GuiObject *boss, const Common::String &prefix, bool lowres) {
	new ButtonWidget(boss, prefix + "ThemeButton", _("Theme:"), Common::U32String(), kChooseThemeCmd);
	_curTheme = new StaticTextWidget(boss, prefix + "CurTheme", g_gui.theme()->getThemeName());
	if (ConfMan.isKeyTemporary("gui_theme"))
		_curTheme->setFontColor(ThemeEngine::FontColor::kFontColorOverride);

	_guiBasePopUpDesc = new StaticTextWidget(boss, prefix + "GUIBasePopupDesc", _("GUI scale:"));
	_guiBasePopUp = new PopUpWidget(boss, prefix + "GUIBasePopup");

	for (int i = 0; guiBaseLabels[i]; i++) {
		_guiBasePopUp->appendEntry(_(guiBaseLabels[i]), guiBaseValues[i]);
	}

	_rendererPopUpDesc = new StaticTextWidget(boss, prefix + "RendererPopupDesc", _("GUI renderer:"));
	_rendererPopUp = new PopUpWidget(boss, prefix + "RendererPopup");

	if (!lowres) {
		for (uint i = 1; i < GUI::ThemeEngine::_rendererModesSize; ++i)
			_rendererPopUp->appendEntry(_(GUI::ThemeEngine::_rendererModes[i].name), GUI::ThemeEngine::_rendererModes[i].mode);
	} else {
		for (uint i = 1; i < GUI::ThemeEngine::_rendererModesSize; ++i)
			_rendererPopUp->appendEntry(_(GUI::ThemeEngine::_rendererModes[i].shortname), GUI::ThemeEngine::_rendererModes[i].mode);
	}

	if (!lowres)
		_autosavePeriodPopUpDesc = new StaticTextWidget(boss, prefix + "AutosavePeriodPopupDesc", _("Autosave:"));
	else
		_autosavePeriodPopUpDesc = new StaticTextWidget(boss, prefix + "AutosavePeriodPopupDesc", _c("Autosave:", "lowres"));
	_autosavePeriodPopUp = new PopUpWidget(boss, prefix + "AutosavePeriodPopup");

	for (int i = 0; savePeriodLabels[i]; i++) {
		_autosavePeriodPopUp->appendEntry(_(savePeriodLabels[i]), savePeriodValues[i]);
	}

	if (!g_system->hasFeature(OSystem::kFeatureNoQuit)) {
		_guiReturnToLauncherAtExit = new CheckboxWidget(boss, prefix + "ReturnToLauncherAtExit",
			_("Return to the launcher when leaving a game"),
			_("Return to the launcher when leaving a game instead of closing ScummVM\n(this feature is not supported by all games).")
		);

		_guiReturnToLauncherAtExit->setState(ConfMan.getBool("gui_return_to_launcher_at_exit", _domain));
	}

	_guiConfirmExit = new CheckboxWidget(boss, prefix + "ConfirmExit",
		_("Ask for confirmation on exit"),
		_("Ask for permission when closing ScummVM or leaving a game.")
	);

	_guiConfirmExit->setState(ConfMan.getBool("confirm_exit", _domain));

#ifdef USE_DISCORD
	_discordRpcCheckbox = new CheckboxWidget(boss, prefix + "DiscordRpc",
		_("Enable Discord integration"),
		_("Show information about the games you are playing on Discord if the Discord client is running.")
	);

	_discordRpcCheckbox->setState(ConfMan.getBool("discord_rpc", _domain));
#endif

	// TODO: joystick setting

#ifdef USE_TRANSLATION
	_guiLanguagePopUpDesc = new StaticTextWidget(boss, prefix + "GuiLanguagePopupDesc", _("GUI language:"), _("Language of ScummVM GUI"));
	_guiLanguagePopUp = new PopUpWidget(boss, prefix + "GuiLanguagePopup");
#ifdef USE_DETECTLANG
	_guiLanguagePopUp->appendEntry(_("<default>"), Common::kTranslationAutodetectId);
#endif // USE_DETECTLANG
	_guiLanguagePopUp->appendEntry("English", Common::kTranslationBuiltinId);
	_guiLanguagePopUp->appendEntry("", 0);
	Common::TLangArray languages = TransMan.getSupportedLanguageNames();
	Common::TLangArray::iterator lang = languages.begin();
	while (lang != languages.end()) {
		_guiLanguagePopUp->appendEntry(lang->name, lang->id);
		lang++;
	}

	// Select the currently configured language or default/English if
	// nothing is specified.
	if (ConfMan.hasKey("gui_language") && !ConfMan.get("gui_language").empty())
		_guiLanguagePopUp->setSelectedTag(TransMan.parseLanguage(ConfMan.get("gui_language")));
	else
#ifdef USE_DETECTLANG
		_guiLanguagePopUp->setSelectedTag(Common::kTranslationAutodetectId);
#else // !USE_DETECTLANG
		_guiLanguagePopUp->setSelectedTag(Common::kTranslationBuiltinId);
#endif // USE_DETECTLANG

	_guiLanguageUseGameLanguageCheckbox = new CheckboxWidget(boss, prefix + "GuiLanguageUseGameLanguage",
			_("Switch the ScummVM GUI language to the game language"),
			_("When starting a game, change the ScummVM GUI language to the game language. "
			"That way, if a game uses the ScummVM save and load dialogs, they are "
			"in the same language as the game.")
	);

	if (ConfMan.hasKey("gui_use_game_language")) {
		_guiLanguageUseGameLanguageCheckbox->setState(ConfMan.getBool("gui_use_game_language", _domain));
	}
#endif // USE_TRANSLATION

	if (g_system->hasFeature(OSystem::kFeatureSystemBrowserDialog)) {
		_useSystemDialogsCheckbox = new CheckboxWidget(boss, prefix + "UseSystemDialogs",
			_("Use native system file browser"),
			_("Use the native system file browser instead of the ScummVM one to select a file or directory.")
		);

		_useSystemDialogsCheckbox->setState(ConfMan.getBool("gui_browser_native", _domain));
	}

#ifdef USE_UPDATES
	if (g_system->getUpdateManager()) {
		_updatesPopUpDesc = new StaticTextWidget(boss, prefix + "UpdatesPopupDesc", _("Update check:"), _("How often to check ScummVM updates"));
		_updatesPopUp = new PopUpWidget(boss, prefix + "UpdatesPopup");

		const int *vals = Common::UpdateManager::getUpdateIntervals();
		while (*vals != -1) {
			_updatesPopUp->appendEntry(Common::UpdateManager::updateIntervalToString(*vals), *vals);
			vals++;
		}

		_updatesPopUp->setSelectedTag(Common::UpdateManager::normalizeInterval(ConfMan.getInt("updates_check")));

		new ButtonWidget(boss, prefix + "UpdatesCheckManuallyButton", _("Check now"), Common::U32String(), kUpdatesCheckCmd);
	}
#endif // USE_UPDATES

#ifdef USE_CLOUD
#ifdef USE_LIBCURL
	new ButtonWidget(boss, prefix + "UpdateIconsButton", _("Update Icons"), Common::U32String(), kUpdateIconsCmd);
#endif
#endif
}

#ifdef USE_CLOUD
#ifdef USE_LIBCURL
void GlobalOptionsDialog::addCloudControls(GuiObject *boss, const Common::String &prefix, bool lowres) {
	_storagePopUpDesc = new StaticTextWidget(boss, prefix + "StoragePopupDesc", _("Active storage:"), _("Active cloud storage"));
	_storagePopUp = new PopUpWidget(boss, prefix + "StoragePopup", Common::U32String(), kStoragePopUpCmd);
	Common::StringArray list = CloudMan.listStorages();
	for (uint32 i = 0; i < list.size(); ++i) {
		_storagePopUp->appendEntry(_(list[i]), i);
	}
	_storagePopUp->setSelected(_selectedStorageIndex);

	if (lowres)
		_storageDisabledHint = new StaticTextWidget(boss, prefix + "StorageDisabledHint", _c("4. Storage is not yet enabled. Verify that username is correct and enable it:", "lowres"));
	else
		_storageDisabledHint = new StaticTextWidget(boss, prefix + "StorageDisabledHint", _("4. Storage is not yet enabled. Verify that username is correct and enable it:"));
	_storageEnableButton = new ButtonWidget(boss, prefix + "StorageEnableButton", _("Enable storage"), _("Confirm you want to use this account for this storage"), kEnableStorageCmd);

	_storageUsernameDesc = new StaticTextWidget(boss, prefix + "StorageUsernameDesc", _("Username:"), _("Username used by this storage"));
	_storageUsername = new StaticTextWidget(boss, prefix + "StorageUsernameLabel", _("<none>"), Common::U32String(), ThemeEngine::kFontStyleNormal);

	_storageUsedSpaceDesc = new StaticTextWidget(boss, prefix + "StorageUsedSpaceDesc", _("Used space:"), _("Space used by ScummVM's saved games on this storage"));
	_storageUsedSpace = new StaticTextWidget(boss, prefix + "StorageUsedSpaceLabel", Common::U32String("0 bytes"), Common::U32String(), ThemeEngine::kFontStyleNormal);

	_storageLastSyncDesc = new StaticTextWidget(boss, prefix + "StorageLastSyncDesc", _("Last sync:"), _("When was the last time saved games were synced with this storage"));
	_storageLastSync = new StaticTextWidget(boss, prefix + "StorageLastSyncLabel", _("<never>"), Common::U32String(), ThemeEngine::kFontStyleNormal);
	if (lowres)
		_storageSyncHint = new StaticTextWidget(boss, prefix + "StorageSyncHint", _c("Saved games sync automatically on launch, after saving and on loading.", "lowres"), Common::U32String(), ThemeEngine::kFontStyleNormal);
	else
		_storageSyncHint = new StaticTextWidget(boss, prefix + "StorageSyncHint", _("Saved games sync automatically on launch, after saving and on loading."), Common::U32String(), ThemeEngine::kFontStyleNormal);
	_storageSyncSavesButton = new ButtonWidget(boss, prefix + "SyncSavesButton", _("Sync now"), _("Start saved games sync"), kSyncSavesStorageCmd);

	if (lowres)
		_storageDownloadHint = new StaticTextWidget(boss, prefix + "StorageDownloadHint", _c("You can download game files from your cloud ScummVM folder:", "lowres"));
	else
		_storageDownloadHint = new StaticTextWidget(boss, prefix + "StorageDownloadHint", _("You can download game files from your cloud ScummVM folder:"));
	_storageDownloadButton = new ButtonWidget(boss, prefix + "DownloadButton", _("Download game files"), _("Open downloads manager dialog"), kDownloadStorageCmd);

	if (lowres)
		_storageDisconnectHint = new StaticTextWidget(boss, prefix + "StorageDisconnectHint", _c("To change account for this storage, disconnect and connect again:", "lowres"));
	else
		_storageDisconnectHint = new StaticTextWidget(boss, prefix + "StorageDisconnectHint", _("To change account for this storage, disconnect and connect again:"));
	_storageDisconnectButton = new ButtonWidget(boss, prefix + "DisconnectButton", _("Disconnect"), _("Stop using this storage on this device"), kDisconnectStorageCmd);

	if (lowres)
		_storageWizardNotConnectedHint = new StaticTextWidget(boss, prefix + "StorageWizardNotConnectedHint", _c("This storage is not connected yet! To connect,", "lowres"));
	else
		_storageWizardNotConnectedHint = new StaticTextWidget(boss, prefix + "StorageWizardNotConnectedHint", _("This storage is not connected yet! To connect,"));
	_storageWizardOpenLinkHint = new StaticTextWidget(boss, prefix + "StorageWizardOpenLinkHint", _("1. Open this link:"));
	_storageWizardLink = new ButtonWidget(boss, prefix + "StorageWizardLink", Common::U32String("https://cloud.scummvm.org/"), _("Open URL"), kOpenUrlStorageCmd);
	if (lowres)
		_storageWizardCodeHint = new StaticTextWidget(boss, prefix + "StorageWizardCodeHint", _c("2. Get the code and enter it here:", "lowres"));
	else
		_storageWizardCodeHint = new StaticTextWidget(boss, prefix + "StorageWizardCodeHint", _("2. Get the code and enter it here:"));
	_storageWizardCodeBox = new EditTextWidget(boss, prefix + "StorageWizardCodeBox", Common::U32String(), Common::U32String(), 0, 0, ThemeEngine::kFontStyleConsole);
	_storageWizardPasteButton = new ButtonWidget(boss, prefix + "StorageWizardPasteButton", _("Paste"), _("Paste code from clipboard"), kPasteCodeStorageCmd);
	_storageWizardConnectButton = new ButtonWidget(boss, prefix + "StorageWizardConnectButton", _("3. Connect"), _("Connect your cloud storage account"), kConnectStorageCmd);
	_storageWizardConnectionStatusHint = new StaticTextWidget(boss, prefix + "StorageWizardConnectionStatusHint", Common::U32String("..."));

	setupCloudTab();
}
#endif // USE_LIBCURL

#ifdef USE_SDL_NET
void GlobalOptionsDialog::addNetworkControls(GuiObject *boss, const Common::String &prefix, bool lowres) {
	_runServerButton = new ButtonWidget(boss, prefix + "RunServerButton", _("Run server"), _("Run local webserver"), kRunServerCmd);
	_serverInfoLabel = new StaticTextWidget(boss, prefix + "ServerInfoLabel", _("Not running"));

	// Root path
	if (lowres)
		_rootPathButton = new ButtonWidget(boss, prefix + "RootPathButton", _c("/root/ Path:", "lowres"), _("Select which directory will be shown as /root/ in the Files Manager"), kChooseRootDirCmd);
	else
		_rootPathButton = new ButtonWidget(boss, prefix + "RootPathButton", _("/root/ Path:"), _("Select which directory will be shown as /root/ in the Files Manager"), kChooseRootDirCmd);
	_rootPath = new StaticTextWidget(boss, prefix + "RootPath", Common::U32String("/foo/bar"), _("Select which directory will be shown as /root/ in the Files Manager"));
	_rootPathClearButton = addClearButton(boss, prefix + "RootPathClearButton", kRootPathClearCmd);

	uint32 port = Networking::LocalWebserver::getPort();

	_serverPortDesc = new StaticTextWidget(boss, prefix + "ServerPortDesc", _("Server's port:"), _("Port for server to use"));
	_serverPort = new EditTextWidget(boss, prefix + "ServerPortEditText", Common::String::format("%u", port), Common::U32String());
	_serverPortClearButton = addClearButton(boss, prefix + "ServerPortClearButton", kServerPortClearCmd);

	if (lowres) {
		_featureDescriptionLine1 = new StaticTextWidget(boss, prefix + "FeatureDescriptionLine1", _c("Run server to manage files with browser (in the same network).", "lowres"), Common::U32String(), ThemeEngine::kFontStyleNormal);
		_featureDescriptionLine2 = new StaticTextWidget(boss, prefix + "FeatureDescriptionLine2", _c("Closing options dialog will stop the server.", "lowres"), Common::U32String(), ThemeEngine::kFontStyleNormal);
	} else {
		_featureDescriptionLine1 = new StaticTextWidget(boss, prefix + "FeatureDescriptionLine1", _("Run server to manage files with browser (in the same network)."), Common::U32String(), ThemeEngine::kFontStyleNormal);
		_featureDescriptionLine2 = new StaticTextWidget(boss, prefix + "FeatureDescriptionLine2", _("Closing options dialog will stop the server."), Common::U32String(), ThemeEngine::kFontStyleNormal);
	}

	reflowNetworkTabLayout();

}
#endif // USE_SDL_NET
#endif // USE_CLOUD

#ifdef USE_TTS
void GlobalOptionsDialog::addAccessibilityControls(GuiObject *boss, const Common::String &prefix) {
	_ttsCheckbox = new CheckboxWidget(boss, prefix + "TTSCheckbox",
			_("Use Text to speech"), _("Will read text in gui on mouse over."));
	if (ConfMan.hasKey("tts_enabled"))
		_ttsCheckbox->setState(ConfMan.getBool("tts_enabled", _domain));
	else
		_ttsCheckbox->setState(false);

	_ttsVoiceSelectionPopUp = new PopUpWidget(boss, prefix + "TTSVoiceSelection");
	Common::TextToSpeechManager *ttsMan = g_system->getTextToSpeechManager();
	Common::Array<Common::TTSVoice> voices;
	if (ttsMan != nullptr)
		voices = ttsMan->getVoicesArray();

	if (voices.empty())
		_ttsVoiceSelectionPopUp->appendEntry(_("None"), 0);
	else {
		_ttsVoiceSelectionPopUp->appendEntry(_("<default>"));
		for(unsigned i = 0; i < voices.size(); i++)
			_ttsVoiceSelectionPopUp->appendEntry(voices[i].getDescription(), i);
	}

	if (ConfMan.hasKey("tts_voice", _domain) && (unsigned) ConfMan.getInt("tts_voice", _domain) < voices.size())
		_ttsVoiceSelectionPopUp->setSelectedTag(ConfMan.getInt("tts_voice", _domain)) ;
	else
		_ttsVoiceSelectionPopUp->setSelected(0);
}
#endif // USE_TTS

struct ExistingSave {
	MetaEngine *metaEngine;
	Common::String target;
	SaveStateDescriptor desc;

	ExistingSave(MetaEngine *_metaEngine, const Common::String &_target, const SaveStateDescriptor &_desc) :
		metaEngine(_metaEngine),
		target(_target),
		desc(_desc)
	{}
};

bool GlobalOptionsDialog::updateAutosavePeriod(int newValue) {
	const int oldAutosavePeriod = ConfMan.getInt("autosave_period");
	if (oldAutosavePeriod != 0 || newValue <= 0)
		return true;
	typedef Common::Array<ExistingSave> ExistingSaveList;
	ExistingSaveList saveList;
	using Common::ConfigManager;
	const int maxListSize = 10;
	bool hasMore = false;
	const ConfigManager::DomainMap &domains = ConfMan.getGameDomains();
	for (ConfigManager::DomainMap::const_iterator it = domains.begin(), end = domains.end(); it != end; ++it) {
		const Common::String target = it->_key;
		const ConfigManager::Domain domain = it->_value;
		// note that engineid isn't present on games that predate it
		// and haven't been run since it was introduced.
		const Common::String engine = domain.getValOrDefault("engineid");
		if (const Plugin *detectionPlugin = EngineMan.findPlugin(engine)) {
			if (const Plugin *plugin = PluginMan.getEngineFromMetaEngine(detectionPlugin)) {
				MetaEngine &metaEngine = plugin->get<MetaEngine>();
				const int autoSaveSlot = metaEngine.getAutosaveSlot();
				if (autoSaveSlot < 0)
					continue;
				SaveStateDescriptor desc = metaEngine.querySaveMetaInfos(target.c_str(), autoSaveSlot);
				if (desc.getSaveSlot() != -1 && !desc.getDescription().empty() && !desc.isAutosave()) {
					if (saveList.size() >= maxListSize) {
						hasMore = true;
						break;
					}
					saveList.push_back(ExistingSave(&metaEngine, target, desc));
				}
			}
		}
	}
	if (!saveList.empty()) {
		Common::U32StringArray altButtons;
		altButtons.push_back(_("Ignore"));
		altButtons.push_back(_("Disable autosave"));
		Common::U32String message = _("WARNING: Autosave was enabled. Some of your games have existing "
				  "saved games on the autosave slot. You can either move the "
				  "existing saves to new slots, disable autosave, or ignore (you "
				  "will be prompted when autosave is about to overwrite a save).\n"
				  "List of games:\n");
		for (ExistingSaveList::const_iterator it = saveList.begin(), end = saveList.end(); it != end; ++it)
			message += Common::U32String(it->target) + Common::U32String(": ") + it->desc.getDescription() + "\n";
		message.deleteLastChar();
		if (hasMore)
			message += _("\nAnd more...");
		GUI::MessageDialog warn(message, _("Move"), altButtons);
		switch (warn.runModal()) {
		case GUI::kMessageOK: {
			ExistingSaveList failedSaves;
			for (ExistingSaveList::const_iterator it = saveList.begin(), end = saveList.end(); it != end; ++it) {
				if (it->metaEngine->copySaveFileToFreeSlot(it->target.c_str(), it->desc.getSaveSlot())) {
					g_system->getSavefileManager()->removeSavefile(
							it->metaEngine->getSavegameFile(it->desc.getSaveSlot(), it->target.c_str()));
				} else {
					failedSaves.push_back(*it);
				}
			}
			if (!failedSaves.empty()) {
				Common::U32String failMessage = _("ERROR: Failed to move the following saved games:\n");
				for (ExistingSaveList::const_iterator it = failedSaves.begin(), end = failedSaves.end(); it != end; ++it)
					failMessage += Common::U32String(it->target) + Common::U32String(": ") + it->desc.getDescription() + "\n";
				failMessage.deleteLastChar();
				GUI::MessageDialog(failMessage).runModal();
			}
			break;
		}
		case GUI::kMessageAlt:
			break;
		case GUI::kMessageAlt + 1:
			return false;
		}
	}
	return true;
}

void GlobalOptionsDialog::apply() {
	OptionsDialog::apply();

	bool isRebuildNeeded = false;

	const auto changePath =
	[&](GUI::StaticTextWidget *const widget, const Common::String &pathType, const Common::U32String &defaultLabel) {
		Common::U32String label(widget->getLabel());
		if (label != ConfMan.get(pathType)) {
			widget->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
			if (label.empty() || (label == defaultLabel))
				ConfMan.removeKey(pathType, _domain);
			else
				ConfMan.set(pathType, label.encode(), _domain);
		}
	};

	changePath(_savePath, "savepath", _("Default"));
	changePath(_themePath, "themepath", _c("None", "path"));
	changePath(_iconPath, "iconspath", _("Default"));
	changePath(_extraPath, "extrapath", _c("None", "path"));

#ifdef DYNAMIC_MODULES
	Common::U32String pluginsPath(_pluginsPath->getLabel());
	if (!pluginsPath.empty() && (pluginsPath != _c("None", "path")))
		ConfMan.set("pluginspath", pluginsPath.encode(), _domain);
	else
		ConfMan.removeKey("pluginspath", _domain);
#endif // DYNAMIC_MODULES

#ifdef USE_CLOUD
#ifdef USE_SDL_NET
	Common::U32String rootPath(_rootPath->getLabel());
	if (!rootPath.empty() && (rootPath != _c("None", "path")))
		ConfMan.set("rootpath", rootPath.encode(), "cloud");
	else
		ConfMan.removeKey("rootpath", "cloud");
#endif // USE_SDL_NET
#endif // USE_CLOUD

	int oldGuiScale = ConfMan.getInt("gui_scale");
	ConfMan.setInt("gui_scale", _guiBasePopUp->getSelectedTag(), _domain);
	if (oldGuiScale != (int)_guiBasePopUp->getSelectedTag())
		g_gui.computeScaleFactor();

	const int autosavePeriod = _autosavePeriodPopUp->getSelectedTag();
	if (updateAutosavePeriod(autosavePeriod))
		ConfMan.setInt("autosave_period", autosavePeriod, _domain);
	else
		_autosavePeriodPopUp->setSelected(0);

#ifdef USE_UPDATES
	if (g_system->getUpdateManager()) {
		ConfMan.setInt("updates_check", _updatesPopUp->getSelectedTag());

		if (_updatesPopUp->getSelectedTag() == Common::UpdateManager::kUpdateIntervalNotSupported) {
			g_system->getUpdateManager()->setAutomaticallyChecksForUpdates(Common::UpdateManager::kUpdateStateDisabled);
		} else {
			g_system->getUpdateManager()->setAutomaticallyChecksForUpdates(Common::UpdateManager::kUpdateStateEnabled);
			g_system->getUpdateManager()->setUpdateCheckInterval(_updatesPopUp->getSelectedTag());
		}
	}
#endif // USE_UPDATES

#ifdef USE_CLOUD
#ifdef USE_LIBCURL
	if (CloudMan.getStorageIndex() != _selectedStorageIndex) {
		if (!CloudMan.switchStorage(_selectedStorageIndex)) {
			bool anotherStorageIsWorking = CloudMan.isWorking();
			Common::U32String message = _("Failed to change cloud storage!");
			if (anotherStorageIsWorking) {
				message += Common::U32String("\n");
				message += _("Another cloud storage is already active.");
			}
			MessageDialog dialog(message);
			dialog.runModal();
		}
	}
#endif // USE_LIBCURL

#ifdef USE_SDL_NET
#ifdef NETWORKING_LOCALWEBSERVER_ENABLE_PORT_OVERRIDE
	// save server's port
	uint32 port = Networking::LocalWebserver::getPort();
	if (_serverPort) {
		uint64 contents = _serverPort->getEditString().asUint64();
		if (contents != 0)
			port = contents;
	}
	ConfMan.setInt("local_server_port", port);
#endif // NETWORKING_LOCALWEBSERVER_ENABLE_PORT_OVERRIDE
#endif // USE_SDL_NET
#endif // USE_CLOUD

	Common::String oldThemeId = g_gui.theme()->getThemeId();
	Common::String oldThemeName = g_gui.theme()->getThemeName();
	if (!_newTheme.empty()) {
		ConfMan.set("gui_theme", _newTheme);
	}

#ifdef USE_TRANSLATION
	int selectedLang = _guiLanguagePopUp->getSelectedTag();
	Common::String oldLang = ConfMan.get("gui_language");
	Common::String newLang = TransMan.getLangById(selectedLang);
	if (newLang != oldLang) {
		TransMan.setLanguage(newLang);
		ConfMan.set("gui_language", newLang);
		isRebuildNeeded = true;
	}
	bool wantsBuiltinLang = TransMan.currentIsBuiltinLanguage();

	bool guiUseGameLanguage = _guiLanguageUseGameLanguageCheckbox->getState();
	ConfMan.setBool("gui_use_game_language", guiUseGameLanguage, _domain);
#endif // USE_TRANSLATION

	if (_useSystemDialogsCheckbox) {
		ConfMan.setBool("gui_browser_native", _useSystemDialogsCheckbox->getState(), _domain);
	}

	if (_guiReturnToLauncherAtExit) {
		ConfMan.setBool("gui_return_to_launcher_at_exit", _guiReturnToLauncherAtExit->getState(), _domain);
	}

	if (_guiConfirmExit) {
		ConfMan.setBool("confirm_exit", _guiConfirmExit->getState(), _domain);
	}
#ifdef USE_DISCORD
	if (_discordRpcCheckbox) {
		ConfMan.setBool("discord_rpc", _discordRpcCheckbox->getState(), _domain);
	}
#endif // USE_DISCORD

	GUI::ThemeEngine::GraphicsMode gfxMode = (GUI::ThemeEngine::GraphicsMode)_rendererPopUp->getSelectedTag();
	Common::String oldGfxConfig = ConfMan.get("gui_renderer");
	Common::String newGfxConfig = GUI::ThemeEngine::findModeConfigName(gfxMode);
	if (newGfxConfig != oldGfxConfig) {
		ConfMan.set("gui_renderer", newGfxConfig, _domain);
	}

	if (_newTheme.empty())
		_newTheme = oldThemeId;

	if (!g_gui.loadNewTheme(_newTheme, gfxMode, true)) {
		Common::U32String errorMessage;

		_curTheme->setLabel(oldThemeName);
		_newTheme = oldThemeId;
		ConfMan.set("gui_theme", _newTheme);
		gfxMode = GUI::ThemeEngine::findMode(oldGfxConfig);
		_rendererPopUp->setSelectedTag(gfxMode);
		newGfxConfig = oldGfxConfig;
		ConfMan.set("gui_renderer", newGfxConfig, _domain);
#ifdef USE_TRANSLATION
		// One reason for failing to load the theme is if we want a language other than
		// the builtin language and the theme does not have unicode fonts for those.
		// We can detect this case as it falls back to the builtin language.
		bool themeLangIssue = (!wantsBuiltinLang && TransMan.currentIsBuiltinLanguage());
		TransMan.setLanguage(oldLang);
		_guiLanguagePopUp->setSelectedTag(selectedLang);
		ConfMan.set("gui_language", oldLang);

		if (themeLangIssue)
			errorMessage = _("Theme does not support selected language!");
		else
#endif // USE_TRANSLATION
			errorMessage = _("Theme cannot be loaded!");

		g_gui.loadNewTheme(_newTheme, gfxMode, true);
		errorMessage += _("\nMisc settings will be restored.");
		MessageDialog error(errorMessage);
		error.runModal();
	}

#ifdef USE_TTS
	Common::TextToSpeechManager *ttsMan = g_system->getTextToSpeechManager();
	if (ttsMan) {
		ttsMan->enable(_ttsCheckbox->getState());
#ifdef USE_TRANSLATION
		if (newLang != oldLang) {
			ttsMan->setLanguage(newLang);
			_ttsVoiceSelectionPopUp->setSelected(0);
		}
#else
		ttsMan->setLanguage("en");
#endif // USE_TRANSLATION

		int volume = (ConfMan.getInt("speech_volume", "scummvm") * 100) / 256;
		if (ConfMan.hasKey("mute", "scummvm") && ConfMan.getBool("mute", "scummvm"))
			volume = 0;
		ttsMan->setVolume(volume);
		ConfMan.setBool("tts_enabled", _ttsCheckbox->getState(), _domain);
		unsigned selectedVoice = _ttsVoiceSelectionPopUp->getSelectedTag();
		ConfMan.setInt("tts_voice", selectedVoice, _domain);
		if (selectedVoice >= ttsMan->getVoicesArray().size())
			selectedVoice = ttsMan->getDefaultVoice();
		ttsMan->setVoice(selectedVoice);
	}
#endif // USE_TTS

	if (isRebuildNeeded) {
		g_gui.setLanguageRTL();
		if (_launcher != nullptr)
			_launcher->rebuild();
		rebuild();
	}

	_newTheme.clear();

	// Save config file
	ConfMan.flushToDisk();
}

void GlobalOptionsDialog::close() {
#if defined(USE_CLOUD) && defined(USE_SDL_NET)
	if (LocalServer.isRunning()) {
		LocalServer.stop();
	}
#endif
	OptionsDialog::close();
}

void GlobalOptionsDialog::handleCommand(CommandSender *sender, uint32 cmd, uint32 data) {
	switch (cmd) {
	case kChooseSaveDirCmd: {
		BrowserDialog browser(_("Select directory for saved games"), true);
		if (browser.runModal() > 0) {
			// User made his choice...
			Common::FSNode dir(browser.getResult());
			if (dir.isWritable()) {
				_savePath->setLabel(dir.getPath());
			} else {
				MessageDialog error(_("The chosen directory cannot be written to. Please select another one."));
				error.runModal();
				return;
			}
			g_gui.scheduleTopDialogRedraw();
		}
		break;
	}
	case kChooseThemeDirCmd: {
		BrowserDialog browser(_("Select directory for GUI themes"), true);
		if (browser.runModal() > 0) {
			// User made his choice...
			Common::FSNode dir(browser.getResult());
			_themePath->setLabel(dir.getPath());
			g_gui.scheduleTopDialogRedraw();
		}
		break;
	}
	case kChooseIconDirCmd: {
		BrowserDialog browser(_("Select directory for GUI launcher thumbnails"), true);
		if (browser.runModal() > 0) {
			// User made his choice...
			Common::FSNode dir(browser.getResult());
			_iconPath->setLabel(dir.getPath());
			g_gui.scheduleTopDialogRedraw();
		}
		break;
	}
	case kChooseExtraDirCmd: {
		BrowserDialog browser(_("Select directory for extra files"), true);
		if (browser.runModal() > 0) {
			// User made his choice...
			Common::FSNode dir(browser.getResult());
			_extraPath->setLabel(dir.getPath());
			g_gui.scheduleTopDialogRedraw();
		}
		break;
	}
#ifdef DYNAMIC_MODULES
	case kChoosePluginsDirCmd: {
		BrowserDialog browser(_("Select directory for plugins"), true);
		if (browser.runModal() > 0) {
			// User made his choice...
			Common::FSNode dir(browser.getResult());
			_pluginsPath->setLabel(dir.getPath());
			g_gui.scheduleTopDialogRedraw();
		}
		break;
	}
#endif
#ifdef USE_CLOUD
#ifdef USE_SDL_NET
	case kChooseRootDirCmd: {
		BrowserDialog browser(_("Select directory for Files Manager /root/"), true);
		if (browser.runModal() > 0) {
			// User made his choice...
			Common::FSNode dir(browser.getResult());
			Common::String path = dir.getPath();
			if (path.empty())
				path = "/"; // absolute root
			_rootPath->setLabel(path);
			g_gui.scheduleTopDialogRedraw();
		}
		break;
	}
#endif

#ifdef USE_LIBCURL
	case kUpdateIconsCmd: {
		DownloadIconsDialog dia;
		dia.runModal();
		break;
	}
#endif

#endif
	case kThemePathClearCmd:
		_themePath->setLabel(_c("None", "path"));
		break;
	case kIconPathClearCmd:
		_iconPath->setLabel(_("Default"));
		break;
	case kExtraPathClearCmd:
		_extraPath->setLabel(_c("None", "path"));
		break;
	case kSavePathClearCmd:
		_savePath->setLabel(_("Default"));
		break;
#ifdef DYNAMIC_MODULES
	case kPluginsPathClearCmd:
		_pluginsPath->setLabel(_c("None", "path"));
		break;
#endif
	case kBrowserPathClearCmd:
		ConfMan.removeKey("browser_lastpath", Common::ConfigManager::kApplicationDomain);
		ConfMan.flushToDisk();
		_browserPath->setLabel(_("Last browser path: ") + _("<default>"));

		break;
#ifdef USE_CLOUD
#ifdef USE_SDL_NET
	case kRootPathClearCmd:
		_rootPath->setLabel(_c("None", "path"));
		break;
#endif
#endif
	case kChooseSoundFontCmd: {
		BrowserDialog browser(_("Select SoundFont"), false);
		if (browser.runModal() > 0) {
			// User made his choice...
			Common::FSNode file(browser.getResult());
			_soundFont->setLabel(file.getPath());

			if (!file.getPath().empty() && (file.getPath().decode() != _c("None", "path")))
				_soundFontClearButton->setEnabled(true);
			else
				_soundFontClearButton->setEnabled(false);

			g_gui.scheduleTopDialogRedraw();
		}
		break;
	}
	case kChooseThemeCmd:
	{
		ThemeBrowser browser;
		if (browser.runModal() > 0) {
			// User made his choice...
			_newTheme = browser.getSelected();
			_curTheme->setLabel(browser.getSelectedName());
			_curTheme->setFontColor(ThemeEngine::FontColor::kFontColorNormal);
		}
		break;
	}
#ifdef USE_CLOUD
#ifdef USE_LIBCURL
	case kCloudTabContainerReflowCmd: {
		setupCloudTab();
		break;
	}
	case kStoragePopUpCmd: {
		if (_storageWizardCodeBox)
			_storageWizardCodeBox->setEditString(Common::U32String());
		// update container's scrollbar
		reflowLayout();
		break;
	}
	case kEnableStorageCmd: {
		CloudMan.enableStorage();
		_redrawCloudTab = true;

		// also, automatically start saves sync when user enables the storage
	}
	// fall through
	case kSyncSavesStorageCmd: {
		CloudMan.syncSaves(
			new Common::Callback<GlobalOptionsDialog, Cloud::Storage::BoolResponse>(this, &GlobalOptionsDialog::storageSavesSyncedCallback)
		);
		break;
	}
	case kDownloadStorageCmd: {
		DownloadDialog dialog(_selectedStorageIndex, _launcher);
		dialog.runModal();
		break;
	}
	case kOpenUrlStorageCmd: {
		Common::String url = "https://cloud.scummvm.org/";
		switch (_selectedStorageIndex) {
		case Cloud::kStorageDropboxId:
			url += "dropbox?refresh_token=true";
			break;
		case Cloud::kStorageOneDriveId:
			url += "onedrive";
			break;
		case Cloud::kStorageGoogleDriveId:
			url += "gdrive";
			break;
		case Cloud::kStorageBoxId:
			url += "box";
			break;
		default:
			break;
		}

		if (!g_system->openUrl(url)) {
			MessageDialog alert(_("Failed to open URL!\nPlease navigate to this page manually."));
			alert.runModal();
		}
		break;
	}
	case kPasteCodeStorageCmd: {
		if (g_system->hasTextInClipboard()) {
			Common::U32String message = g_system->getTextFromClipboard();
			if (!message.empty()) {
				_storageWizardCodeBox->setEditString(message);
				_redrawCloudTab = true;
			}
		}
		break;
	}
	case kConnectStorageCmd: {
		Common::String code = "";
		if (_storageWizardCodeBox)
			code = _storageWizardCodeBox->getEditString().encode();
		if (code.size() == 0)
			return;

		if (CloudMan.isWorking()) {
			bool cancel = true;

			MessageDialog alert(_("Another Storage is working now. Do you want to interrupt it?"), _("Yes"), _("No"));
			if (alert.runModal() == GUI::kMessageOK) {
				if (CloudMan.isDownloading())
					CloudMan.cancelDownload();
				if (CloudMan.isSyncing())
					CloudMan.cancelSync();

				// I believe it still would return `true` here, but just in case
				if (CloudMan.isWorking()) {
					MessageDialog alert2(_("Wait until current Storage finishes up and try again."));
					alert2.runModal();
				} else {
					cancel = false;
				}
			}

			if (cancel) {
				return;
			}
		}

		if (_storageWizardConnectionStatusHint)
			_storageWizardConnectionStatusHint->setLabel(_("Connecting..."));
		CloudMan.connectStorage(
			_selectedStorageIndex, code,
			new Common::Callback<GlobalOptionsDialog, Networking::ErrorResponse>(this, &GlobalOptionsDialog::storageConnectionCallback)
		);
		_connectingStorage = true;
		_redrawCloudTab = true;
		break;
	}
	case kDisconnectStorageCmd: {
		if (_storageWizardCodeBox)
			_storageWizardCodeBox->setEditString(Common::U32String());

		if (_selectedStorageIndex == CloudMan.getStorageIndex() && CloudMan.isWorking()) {
			bool cancel = true;

			MessageDialog alert(_("This Storage is working now. Do you want to interrupt it?"), _("Yes"), _("No"));
			if (alert.runModal() == GUI::kMessageOK) {
				if (CloudMan.isDownloading())
					CloudMan.cancelDownload();
				if (CloudMan.isSyncing())
					CloudMan.cancelSync();

				// I believe it still would return `true` here, but just in case
				if (CloudMan.isWorking()) {
					MessageDialog alert2(_("Wait until current Storage finishes up and try again."));
					alert2.runModal();
				} else {
					cancel = false;
				}
			}

			if (cancel) {
				return;
			}
		}

		CloudMan.disconnectStorage(_selectedStorageIndex);
		_redrawCloudTab = true;
		sendCommand(kSetPositionCmd, 0);
		break;
	}
#endif // USE_LIBCURL
#ifdef USE_SDL_NET
	case kRunServerCmd: {
#ifdef NETWORKING_LOCALWEBSERVER_ENABLE_PORT_OVERRIDE
		// save server's port
		uint32 port = Networking::LocalWebserver::getPort();
		if (_serverPort) {
			uint64 contents = _serverPort->getEditString().asUint64();
			if (contents != 0)
				port = contents;
		}
		ConfMan.setInt("local_server_port", port);
		ConfMan.flushToDisk();
#endif // NETWORKING_LOCALWEBSERVER_ENABLE_PORT_OVERRIDE

		if (LocalServer.isRunning())
			LocalServer.stopOnIdle();
		else
			LocalServer.start();

		break;
	}
	case kServerPortClearCmd: {
		if (_serverPort) {
			_serverPort->setEditString(Common::String::format("%u", Networking::LocalWebserver::DEFAULT_SERVER_PORT));
		}
		g_gui.scheduleTopDialogRedraw();
		break;
	}
#endif // USE_SDL_NET
#endif // USE_CLOUD
#ifdef USE_FLUIDSYNTH
	case kFluidSynthSettingsCmd:
		_fluidSynthSettingsDialog->runModal();
		break;
#endif
#ifdef USE_UPDATES
	case kUpdatesCheckCmd:
		if (g_system->getUpdateManager())
			g_system->getUpdateManager()->checkForUpdates();
		break;
#endif
	default:
		OptionsDialog::handleCommand(sender, cmd, data);
	}
}

void GlobalOptionsDialog::handleTickle() {
	OptionsDialog::handleTickle();
#ifdef USE_CLOUD
#ifdef USE_LIBCURL
	if (_redrawCloudTab) {
		reflowLayout(); // recalculates scrollbar as well
		_redrawCloudTab = false;
	}
#endif // USE_LIBCURL
#ifdef USE_SDL_NET
	if (LocalServer.isRunning() != _serverWasRunning) {
		_serverWasRunning = !_serverWasRunning;
		reflowNetworkTabLayout();
	}
#endif // USE_SDL_NET
#endif // USE_CLOUD
}

void GlobalOptionsDialog::reflowLayout() {
	int firstVisible = _tabWidget->getFirstVisible();
	int activeTab = _tabWidget->getActiveTab();

	if (_midiTabId != -1) {
		_tabWidget->setActiveTab(_midiTabId);

		bool enabled = _soundFontClearButton->isEnabled();
		_tabWidget->removeWidget(_soundFontClearButton);
		_soundFontClearButton->setNext(nullptr);
		delete _soundFontClearButton;
		_soundFontClearButton = addClearButton(_tabWidget, "GlobalOptions_MIDI.mcFontClearButton", kClearSoundFontCmd);
		_soundFontClearButton->setEnabled(enabled);
	}

	if (_pathsTabId != -1) {
		_tabWidget->setActiveTab(_pathsTabId);

		_tabWidget->removeWidget(_savePathClearButton);
		_savePathClearButton->setNext(nullptr);
		delete _savePathClearButton;
		_savePathClearButton = addClearButton(_tabWidget, "GlobalOptions_Paths.SavePathClearButton", kSavePathClearCmd);

		_tabWidget->removeWidget(_themePathClearButton);
		_themePathClearButton->setNext(nullptr);
		delete _themePathClearButton;
		_themePathClearButton = addClearButton(_tabWidget, "GlobalOptions_Paths.ThemePathClearButton", kThemePathClearCmd);

		_tabWidget->removeWidget(_iconPathClearButton);
		_iconPathClearButton->setNext(nullptr);
		delete _iconPathClearButton;
		_iconPathClearButton = addClearButton(_tabWidget, "GlobalOptions_Paths.IconPathClearButton", kIconPathClearCmd);

		_tabWidget->removeWidget(_extraPathClearButton);
		_extraPathClearButton->setNext(nullptr);
		delete _extraPathClearButton;
		_extraPathClearButton = addClearButton(_tabWidget, "GlobalOptions_Paths.ExtraPathClearButton", kExtraPathClearCmd);

		_tabWidget->removeWidget(_browserPathClearButton);
		_browserPathClearButton->setNext(nullptr);
		delete _browserPathClearButton;
		_browserPathClearButton = addClearButton(_tabWidget, "GlobalOptions_Paths.BrowserPathClearButton", kBrowserPathClearCmd);
	}

	_tabWidget->setActiveTab(activeTab);
	_tabWidget->setFirstVisible(firstVisible);

	OptionsDialog::reflowLayout();
#ifdef USE_CLOUD
#ifdef USE_LIBCURL
	setupCloudTab();
#endif // USE_LIBCURL
#ifdef USE_SDL_NET
	reflowNetworkTabLayout();
#endif // USE_SDL_NET
#endif // USE_CLOUD
}

#ifdef USE_CLOUD
#ifdef USE_LIBCURL
void GlobalOptionsDialog::setupCloudTab() {
	_selectedStorageIndex = (_storagePopUp ? _storagePopUp->getSelectedTag() : (uint32)Cloud::kStorageNoneId);

	if (_storagePopUpDesc) _storagePopUpDesc->setVisible(true);
	if (_storagePopUp) _storagePopUp->setVisible(true);

	Common::String username = CloudMan.getStorageUsername(_selectedStorageIndex);
	bool storageConnected = (username != "");
	bool shown = (_selectedStorageIndex != Cloud::kStorageNoneId);
	bool shownConnectedInfo = (shown && storageConnected);
	bool showingCurrentStorage = (shownConnectedInfo && _selectedStorageIndex == CloudMan.getStorageIndex());
	bool enabled = (shownConnectedInfo && CloudMan.isStorageEnabled());

	// there goes layout for connected Storage

	if (_storageDisabledHint) _storageDisabledHint->setVisible(showingCurrentStorage && !enabled);
	if (_storageEnableButton) _storageEnableButton->setVisible(showingCurrentStorage && !enabled);

	// calculate shift
	int16 x, y;
	int16 w, h;
	int16 shiftUp = 0;
	if (!showingCurrentStorage || enabled) {
		// "storage is disabled" hint is not shown, shift everything up
		if (!g_gui.xmlEval()->getWidgetData("GlobalOptions_Cloud_Container.StorageDisabledHint", x, y, w, h))
			warning("GlobalOptions_Cloud_Container.StorageUsernameDesc's position is undefined");
		shiftUp = y;
		if (!g_gui.xmlEval()->getWidgetData("GlobalOptions_Cloud_Container.StorageUsernameDesc", x, y, w, h))
			warning("GlobalOptions_Cloud_Container.StorageWizardNotConnectedHint's position is undefined");
		shiftUp = y - shiftUp;
	}

	if (_storageUsernameDesc) _storageUsernameDesc->setVisible(shownConnectedInfo);
	if (_storageUsername) {
		_storageUsername->setLabel(username);
		_storageUsername->setVisible(shownConnectedInfo);
	}
	if (_storageUsedSpaceDesc) _storageUsedSpaceDesc->setVisible(shownConnectedInfo);
	if (_storageUsedSpace) {
		uint64 usedSpace = CloudMan.getStorageUsedSpace(_selectedStorageIndex);
		Common::String usedSpaceNumber, usedSpaceUnits;
		usedSpaceNumber = Common::getHumanReadableBytes(usedSpace, usedSpaceUnits);
		_storageUsedSpace->setLabel(Common::U32String::format("%s %S", usedSpaceNumber.c_str(), _(usedSpaceUnits).c_str()));
		_storageUsedSpace->setVisible(shownConnectedInfo);
	}
	if (_storageSyncHint) {
		_storageSyncHint->setVisible(shownConnectedInfo);
		_storageSyncHint->setEnabled(false);
	}
	if (_storageLastSyncDesc) _storageLastSyncDesc->setVisible(shownConnectedInfo);
	if (_storageLastSync) {
		Common::U32String sync = CloudMan.getStorageLastSync(_selectedStorageIndex);
		if (sync == "") {
			if (_selectedStorageIndex == CloudMan.getStorageIndex() && CloudMan.isSyncing())
				sync = _("<right now>");
			else
				sync = _("<never>");
		}
		_storageLastSync->setLabel(sync);
		_storageLastSync->setVisible(shownConnectedInfo);
	}
	if (_storageSyncSavesButton) {
		_storageSyncSavesButton->setVisible(showingCurrentStorage);
		_storageSyncSavesButton->setEnabled(enabled);
	}

	bool showDownloadButton = (showingCurrentStorage && _selectedStorageIndex != Cloud::kStorageGoogleDriveId); // cannot download via Google Drive
	if (_storageDownloadHint) _storageDownloadHint->setVisible(showDownloadButton);
	if (_storageDownloadButton) {
		_storageDownloadButton->setVisible(showDownloadButton);
		_storageDownloadButton->setEnabled(enabled);
	}
	if (_storageDisconnectHint) _storageDisconnectHint->setVisible(shownConnectedInfo);
	if (_storageDisconnectButton) _storageDisconnectButton->setVisible(shownConnectedInfo);

	int16 disconnectWidgetsAdditionalShift = 0;
	if (!showDownloadButton) {
		if (!g_gui.xmlEval()->getWidgetData("GlobalOptions_Cloud_Container.StorageDownloadHint", x, y, w, h))
			warning("GlobalOptions_Cloud_Container.StorageDownloadHint's position is undefined");
		disconnectWidgetsAdditionalShift = y;
		if (!g_gui.xmlEval()->getWidgetData("GlobalOptions_Cloud_Container.StorageDisconnectHint", x, y, w, h))
			warning("GlobalOptions_Cloud_Container.DownloadButton's position is undefined");
		disconnectWidgetsAdditionalShift = y - disconnectWidgetsAdditionalShift;
	}

	shiftWidget(_storageUsernameDesc, "GlobalOptions_Cloud_Container.StorageUsernameDesc", 0, -shiftUp);
	shiftWidget(_storageUsername, "GlobalOptions_Cloud_Container.StorageUsernameLabel", 0, -shiftUp);
	shiftWidget(_storageUsedSpaceDesc, "GlobalOptions_Cloud_Container.StorageUsedSpaceDesc", 0, -shiftUp);
	shiftWidget(_storageUsedSpace, "GlobalOptions_Cloud_Container.StorageUsedSpaceLabel", 0, -shiftUp);
	shiftWidget(_storageSyncHint, "GlobalOptions_Cloud_Container.StorageSyncHint", 0, -shiftUp);
	shiftWidget(_storageLastSyncDesc, "GlobalOptions_Cloud_Container.StorageLastSyncDesc", 0, -shiftUp);
	shiftWidget(_storageLastSync, "GlobalOptions_Cloud_Container.StorageLastSyncLabel", 0, -shiftUp);
	shiftWidget(_storageSyncSavesButton, "GlobalOptions_Cloud_Container.SyncSavesButton", 0, -shiftUp);
	shiftWidget(_storageDownloadHint, "GlobalOptions_Cloud_Container.StorageDownloadHint", 0, -shiftUp);
	shiftWidget(_storageDownloadButton, "GlobalOptions_Cloud_Container.DownloadButton", 0, -shiftUp);
	shiftWidget(_storageDisconnectHint, "GlobalOptions_Cloud_Container.StorageDisconnectHint", 0, -shiftUp - disconnectWidgetsAdditionalShift);
	shiftWidget(_storageDisconnectButton, "GlobalOptions_Cloud_Container.DisconnectButton", 0, -shiftUp - disconnectWidgetsAdditionalShift);

	// there goes layout for non-connected Storage (connection wizard)

	shown = (!shownConnectedInfo && shown);
	bool wizardEnabled = !_connectingStorage;
	if (_storageWizardNotConnectedHint) _storageWizardNotConnectedHint->setVisible(shown);
	if (_storageWizardOpenLinkHint) _storageWizardOpenLinkHint->setVisible(shown);
	if (_storageWizardLink) {
		_storageWizardLink->setVisible(shown);
		_storageWizardLink->setEnabled(g_system->hasFeature(OSystem::kFeatureOpenUrl) && wizardEnabled);
	}
	if (_storageWizardCodeHint) _storageWizardCodeHint->setVisible(shown);
	if (_storageWizardCodeBox) {
		_storageWizardCodeBox->setVisible(shown);
		_storageWizardCodeBox->setEnabled(wizardEnabled);
	}
	if (_storageWizardPasteButton) {
		_storageWizardPasteButton->setVisible(shown && g_system->hasFeature(OSystem::kFeatureClipboardSupport));
		_storageWizardPasteButton->setEnabled(wizardEnabled);
	}
	if (_storageWizardConnectButton) {
		_storageWizardConnectButton->setVisible(shown);
		_storageWizardConnectButton->setEnabled(wizardEnabled);
	}
	if (_storageWizardConnectionStatusHint) {
		_storageWizardConnectionStatusHint->setVisible(shown && _storageWizardConnectionStatusHint->getLabel() != "...");
		_storageWizardConnectionStatusHint->setEnabled(wizardEnabled);
	}

	if (!shownConnectedInfo) {
		if (!g_gui.xmlEval()->getWidgetData("GlobalOptions_Cloud_Container.StorageDisabledHint", x, y, w, h))
			warning("GlobalOptions_Cloud_Container.StorageUsernameDesc's position is undefined");
		shiftUp = y;
		if (!g_gui.xmlEval()->getWidgetData("GlobalOptions_Cloud_Container.StorageWizardNotConnectedHint", x, y, w, h))
			warning("GlobalOptions_Cloud_Container.StorageWizardNotConnectedHint's position is undefined");
		shiftUp = y - shiftUp;

		shiftWidget(_storageWizardNotConnectedHint, "GlobalOptions_Cloud_Container.StorageWizardNotConnectedHint", 0, -shiftUp);
		shiftWidget(_storageWizardOpenLinkHint, "GlobalOptions_Cloud_Container.StorageWizardOpenLinkHint", 0, -shiftUp);
		shiftWidget(_storageWizardLink, "GlobalOptions_Cloud_Container.StorageWizardLink", 0, -shiftUp);
		shiftWidget(_storageWizardCodeHint, "GlobalOptions_Cloud_Container.StorageWizardCodeHint", 0, -shiftUp);
		shiftWidget(_storageWizardCodeBox, "GlobalOptions_Cloud_Container.StorageWizardCodeBox", 0, -shiftUp);
		shiftWidget(_storageWizardPasteButton, "GlobalOptions_Cloud_Container.StorageWizardPasteButton", 0, -shiftUp);
		shiftWidget(_storageWizardConnectButton, "GlobalOptions_Cloud_Container.StorageWizardConnectButton", 0, -shiftUp);
		shiftWidget(_storageWizardConnectionStatusHint, "GlobalOptions_Cloud_Container.StorageWizardConnectionStatusHint", 0, -shiftUp);
	}
}

void GlobalOptionsDialog::shiftWidget(Widget *widget, const char *widgetName, int32 xOffset, int32 yOffset) {
	if (!widget) return;

	int16 x, y;
	int16 w, h;
	if (!g_gui.xmlEval()->getWidgetData(widgetName, x, y, w, h))
		warning("%s's position is undefined", widgetName);

	widget->setPos(x + xOffset, y + yOffset);
}
#endif // USE_LIBCURL

#ifdef USE_SDL_NET
void GlobalOptionsDialog::reflowNetworkTabLayout() {
	bool serverIsRunning = LocalServer.isRunning();

	if (_runServerButton) {
		_runServerButton->setVisible(true);
		_runServerButton->setLabel(_(serverIsRunning ? "Stop server" : "Run server"));
		_runServerButton->setTooltip(_(serverIsRunning ? "Stop local webserver" : "Run local webserver"));
	}
	if (_serverInfoLabel) {
		_serverInfoLabel->setVisible(true);
		if (serverIsRunning)
			_serverInfoLabel->setLabel(LocalServer.getAddress());
		else
			_serverInfoLabel->setLabel(_("Not running"));
	}
	if (_rootPathButton) _rootPathButton->setVisible(true);
	if (_rootPath) _rootPath->setVisible(true);
	if (_rootPathClearButton) _rootPathClearButton->setVisible(true);
#ifdef NETWORKING_LOCALWEBSERVER_ENABLE_PORT_OVERRIDE
	if (_serverPortDesc) {
		_serverPortDesc->setVisible(true);
		_serverPortDesc->setEnabled(!serverIsRunning);
	}
	if (_serverPort) {
		_serverPort->setVisible(true);
		_serverPort->setEnabled(!serverIsRunning);
	}
	if (_serverPortClearButton) {
		_serverPortClearButton->setVisible(true);
		_serverPortClearButton->setEnabled(!serverIsRunning);
	}
#else // NETWORKING_LOCALWEBSERVER_ENABLE_PORT_OVERRIDE
	if (_serverPortDesc)
		_serverPortDesc->setVisible(false);
	if (_serverPort)
		_serverPort->setVisible(false);
	if (_serverPortClearButton)
		_serverPortClearButton->setVisible(false);
#endif // NETWORKING_LOCALWEBSERVER_ENABLE_PORT_OVERRIDE

	// if port override isn't supported, there will be a gap between these lines and options -- it's OK

	if (_featureDescriptionLine1) {
		_featureDescriptionLine1->setVisible(true);
		_featureDescriptionLine1->setEnabled(false);
	}
	if (_featureDescriptionLine2) {
		_featureDescriptionLine2->setVisible(true);
		_featureDescriptionLine2->setEnabled(false);
	}
}
#endif // USE_SDL_NET

#ifdef USE_LIBCURL
void GlobalOptionsDialog::storageConnectionCallback(Networking::ErrorResponse response) {
	Common::U32String message("...");
	if (!response.failed && !response.interrupted) {
		// success
		g_system->displayMessageOnOSD(_("Storage connected."));
	} else {
		message = _("Failed to connect storage.");
		if (response.failed) {
			message = _("Failed to connect storage: ") + _(response.response.c_str());
		}
	}

	if (_storageWizardConnectionStatusHint)
		_storageWizardConnectionStatusHint->setLabel(message);

	_redrawCloudTab = true;
	_connectingStorage = false;
}

void GlobalOptionsDialog::storageSavesSyncedCallback(Cloud::Storage::BoolResponse response) {
	_redrawCloudTab = true;
}

void GlobalOptionsDialog::storageErrorCallback(Networking::ErrorResponse response) {
	debug(9, "GlobalOptionsDialog: error response (%s, %ld):", (response.failed ? "failed" : "interrupted"), response.httpResponseCode);
	debug(9, "%s", response.response.c_str());

	if (!response.interrupted)
		g_system->displayMessageOnOSD(_("Request failed.\nCheck your Internet connection."));
}
#endif // USE_LIBCURL
#endif // USE_CLOUD

bool OptionsDialog::testGraphicsSettings() {
	int xres = 320, yres = 240;

	g_system->beginGFXTransaction();
	g_system->initSize(xres, yres);
	g_system->endGFXTransaction();

	Graphics::ManagedSurface *pm5544 = Graphics::renderPM5544(xres, yres);

	byte palette[768];
	const uint32 *p = pm5544->getPalette();

	for (int i = 0; i < 256; i++) {
		palette[i * 3 + 0] =  p[i]        & 0xff;
		palette[i * 3 + 1] = (p[i] >> 8)  & 0xff;
		palette[i * 3 + 2] = (p[i] >> 16) & 0xff;
	}

	g_system->getPaletteManager()->setPalette(palette, 0, 256);

	g_system->copyRectToScreen(pm5544->surfacePtr()->getPixels(), pm5544->surfacePtr()->pitch, 0, 0, xres, yres);
	g_system->updateScreen();

	delete pm5544;

	// And display the error
	GUI::CountdownMessageDialog dialog(_("A test pattern should be displayed.\nDo you want to keep these shader scaler settings?"),
				10000,
				_("Yes"), _("No"), Graphics::kTextAlignCenter,
				_("Reverting automatically in %d seconds"));

	g_gui.displayTopDialogOnly(true);

	bool retval = (dialog.runModal() == GUI::kMessageOK);

	g_gui.displayTopDialogOnly(false);

	// Clear screen so we do not see any artefacts
	g_system->fillScreen(0);
	g_system->updateScreen();

	return retval;
}

} // End of namespace GUI
