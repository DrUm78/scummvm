/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include <unistd.h>
#include <sys/time.h>
#include <list>

#include <retro_miscellaneous.h>
#include <retro_inline.h>

#include "surface.libretro.h"
#include "backends/base-backend.h"
#include "common/events.h"
#include "common/config-manager.h"
#include "audio/mixer_intern.h"

#if defined(_WIN32)
#include "backends/fs/windows/windows-fs-factory.h"
#define FS_SYSTEM_FACTORY WindowsFilesystemFactory
#else
#include "libretro-fs-factory.h"
#define FS_SYSTEM_FACTORY LibRetroFilesystemFactory
#endif

#include "backends/timer/default/default-timer.h"
#include "graphics/colormasks.h"
#include "graphics/palette.h"
#include "backends/saves/default/default-saves.h"
#if defined(_WIN32)
#include <direct.h>
#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif
#elif defined(__CELLOS_LV2__)
#include <sys/sys_time.h>
#elif (defined(GEKKO) && !defined(WIIU))
#include <ogc/lwp_watchdog.h>
#else
#include <time.h>
#endif

#include "libretro.h"
#include "retro_emu_thread.h"

extern retro_log_printf_t log_cb;

#include "common/mutex.h"

/**
 *  Dummy mutex implementation
 */
class LibretroMutexInternal final : public Common::MutexInternal {
public:
	LibretroMutexInternal() {};
	~LibretroMutexInternal() override {};

	bool lock() override { return 0; }
	bool unlock() override { return 0; };
};

Common::MutexInternal *createLibretroMutexInternal() {
	return new LibretroMutexInternal();
}

struct RetroPalette
{
   unsigned char _colors[256 * 3];

   RetroPalette()
   {
      memset(_colors, 0, sizeof(_colors));
   }

   void set(const byte *colors, uint start, uint num)
   {
      memcpy(_colors + start * 3, colors, num * 3);
   }

   void get(byte* colors, uint start, uint num) const
   {
      memcpy(colors, _colors + start * 3, num * 3);
   }

   unsigned char *getColor(uint aIndex) const
   {
      return (unsigned char*)&_colors[aIndex * 3];
   }
};

static INLINE void blit_uint8_uint16_fast(Graphics::Surface& aOut, const Graphics::Surface& aIn, const RetroPalette& aColors)
{
   for(int i = 0; i < aIn.h; i ++)
   {
      if(i >= aOut.h)
         continue;

      uint8_t * const in  = (uint8_t*)aIn.pixels + (i * aIn.w);
      uint16_t* const out = (uint16_t*)aOut.pixels + (i * aOut.w);

      for(int j = 0; j < aIn.w; j ++)
      {
         if (j >= aOut.w)
            continue;

         uint8 r, g, b;

         const uint8_t val = in[j];
         if(val != 0xFFFFFFFF)
         {
            if(aIn.format.bytesPerPixel == 1)
            {
               unsigned char *col = aColors.getColor(val);
               r = *col++;
               g = *col++;
               b = *col++;
            }
            else
               aIn.format.colorToRGB(in[j], r, g, b);

            out[j] = aOut.format.RGBToColor(r, g, b);
         }
      }
   }
}

static INLINE void blit_uint32_uint16(Graphics::Surface& aOut, const Graphics::Surface& aIn, const RetroPalette& aColors)
{
   for(int i = 0; i < aIn.h; i ++)
   {
      if(i >= aOut.h)
         continue;

      uint32_t* const in = (uint32_t*)aIn.pixels + (i * aIn.w);
      uint16_t* const out = (uint16_t*)aOut.pixels + (i * aOut.w);

      for(int j = 0; j < aIn.w; j ++)
      {
         if(j >= aOut.w)
            continue;

         uint8 r, g, b;

         const uint32_t val = in[j];
         if(val != 0xFFFFFFFF)
         {
            aIn.format.colorToRGB(in[j], r, g, b);
            out[j] = aOut.format.RGBToColor(r, g, b);
         }
      }
   }
}

static INLINE void blit_uint16_uint16(Graphics::Surface& aOut, const Graphics::Surface& aIn, const RetroPalette& aColors)
{
   for(int i = 0; i < aIn.h; i ++)
   {
      if(i >= aOut.h)
         continue;

      uint16_t* const in = (uint16_t*)aIn.pixels + (i * aIn.w);
      uint16_t* const out = (uint16_t*)aOut.pixels + (i * aOut.w);

      for(int j = 0; j < aIn.w; j ++)
      {
         if(j >= aOut.w)
            continue;

         uint8 r, g, b;

         const uint16_t val = in[j];
         if(val != 0xFFFFFFFF)
         {
            aIn.format.colorToRGB(in[j], r, g, b);
            out[j] = aOut.format.RGBToColor(r, g, b);
         }
      }
   }
}

static void blit_uint8_uint16(Graphics::Surface& aOut, const Graphics::Surface& aIn, int aX, int aY, const RetroPalette& aColors, uint32 aKeyColor)
{
   for(int i = 0; i < aIn.h; i ++)
   {
      if((i + aY) < 0 || (i + aY) >= aOut.h)
         continue;

      uint8_t* const in = (uint8_t*)aIn.pixels + (i * aIn.w);
      uint16_t* const out = (uint16_t*)aOut.pixels + ((i + aY) * aOut.w);

      for(int j = 0; j < aIn.w; j ++)
      {
         if((j + aX) < 0 || (j + aX) >= aOut.w)
            continue;

         uint8 r, g, b;

         const uint8_t val = in[j];
         if(val != aKeyColor)
         {
            unsigned char *col = aColors.getColor(val);
            r = *col++;
            g = *col++;
            b = *col++;
            out[j + aX] = aOut.format.RGBToColor(r, g, b);
         }
      }
   }
}

static void blit_uint16_uint16(Graphics::Surface& aOut, const Graphics::Surface& aIn, int aX, int aY, const RetroPalette& aColors, uint32 aKeyColor)
{
   for(int i = 0; i < aIn.h; i ++)
   {
      if((i + aY) < 0 || (i + aY) >= aOut.h)
         continue;

      uint16_t* const in = (uint16_t*)aIn.pixels + (i * aIn.w);
      uint16_t* const out = (uint16_t*)aOut.pixels + ((i + aY) * aOut.w);

      for(int j = 0; j < aIn.w; j ++)
      {
         if((j + aX) < 0 || (j + aX) >= aOut.w)
            continue;

         uint8 r, g, b;

         const uint16_t val = in[j];
         if(val != aKeyColor)
         {
            aIn.format.colorToRGB(in[j], r, g, b);
            out[j + aX] = aOut.format.RGBToColor(r, g, b);
         }
      }
   }
}

static INLINE void copyRectToSurface(uint8_t *pixels, int out_pitch, const uint8_t *src, int pitch, int x, int y, int w, int h, int out_bpp)
{
   uint8_t *dst = pixels + y * out_pitch + x * out_bpp;

   do
   {
      memcpy(dst, src, w * out_bpp);
      src += pitch;
      dst += out_pitch;
   }while(--h);
}

static Common::String s_systemDir;
static Common::String s_saveDir;

#ifdef FRONTEND_SUPPORTS_RGB565
#define SURF_BPP 2
#define SURF_RBITS 2
#define SURF_GBITS 5
#define SURF_BBITS 6
#define SURF_ABITS 5
#define SURF_ALOSS (8-SURF_ABITS)
#define SURF_RLOSS (8-SURF_RBITS)
#define SURF_GLOSS (8-SURF_GBITS)
#define SURF_BLOSS (8-SURF_BBITS)
#define SURF_RSHIFT 0
#define SURF_GSHIFT 11
#define SURF_BSHIFT 5
#define SURF_ASHIFT 0
#else
#define SURF_BPP 2
#define SURF_RBITS 5
#define SURF_GBITS 5
#define SURF_BBITS 5
#define SURF_ABITS 1
#define SURF_ALOSS (8-SURF_ABITS)
#define SURF_RLOSS (8-SURF_RBITS)
#define SURF_GLOSS (8-SURF_GBITS)
#define SURF_BLOSS (8-SURF_BBITS)
#define SURF_RSHIFT 10
#define SURF_GSHIFT 5
#define SURF_BSHIFT 0
#define SURF_ASHIFT 15
#endif

std::list<Common::Event> _events;

class OSystem_RETRO : public EventsBaseBackend, public PaletteManager {
   public:
      Graphics::Surface _screen;

      Graphics::Surface _gameScreen;
      RetroPalette _gamePalette;

      Graphics::Surface _overlay;
      bool _overlayVisible;
      bool _overlayInGUI;

      Graphics::Surface _mouseImage;
      RetroPalette _mousePalette;
      bool _mousePaletteEnabled;
      bool _mouseVisible;
      int _mouseX;
      int _mouseY;
      float _mouseXAcc;
      float _mouseYAcc;
      float _dpadXAcc;
      float _dpadYAcc;
      float _dpadXVel;
      float _dpadYVel;
      int _mouseHotspotX;
      int _mouseHotspotY;
      int _mouseKeyColor;
      bool _mouseDontScale;
      bool _mouseButtons[2];
      bool _joypadmouseButtons[2];
      bool _joypadkeyboardButtons[8];
      unsigned _joypadnumpadLast;
      bool _joypadnumpadActive;
      bool _ptrmouseButton;

      uint32 _startTime;
      uint32 _threadExitTime;
      
      bool _speed_hack_enabled;


      Audio::MixerImpl* _mixer;


      OSystem_RETRO(bool aEnableSpeedHack) :
         _mousePaletteEnabled(false), _mouseVisible(false),
         _mouseX(0), _mouseY(0), _mouseXAcc(0.0), _mouseYAcc(0.0), _mouseHotspotX(0), _mouseHotspotY(0),
		 _dpadXAcc(0.0), _dpadYAcc(0.0), _dpadXVel(0.0f), _dpadYVel(0.0f),
         _mouseKeyColor(0), _mouseDontScale(false),
         _joypadnumpadLast(8), _joypadnumpadActive(false),
         _mixer(0), _startTime(0), _threadExitTime(10),
         _speed_hack_enabled(aEnableSpeedHack)
   {
      _fsFactory = new FS_SYSTEM_FACTORY();
      memset(_mouseButtons, 0, sizeof(_mouseButtons));
      memset(_joypadmouseButtons, 0, sizeof(_joypadmouseButtons));
      memset(_joypadkeyboardButtons, 0, sizeof(_joypadkeyboardButtons));

      _startTime = getMillis();

      if(s_systemDir.empty())
         s_systemDir = ".";

      if(s_saveDir.empty())
         s_saveDir = ".";
   }

      virtual ~OSystem_RETRO()
      {
         _gameScreen.free();
         _overlay.free();
         _mouseImage.free();
         _screen.free();

         delete _mixer;
      }

      virtual void initBackend()
      {
         _savefileManager = new DefaultSaveFileManager(s_saveDir);
#ifdef FRONTEND_SUPPORTS_RGB565
         _overlay.create(RES_W_OVERLAY, RES_H_OVERLAY, Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
#else
         _overlay.create(RES_W_OVERLAY, RES_H_OVERLAY, Graphics::PixelFormat(2, 5, 5, 5, 1, 10, 5, 0, 15));
#endif
         _mixer = new Audio::MixerImpl(48000);
         _timerManager = new DefaultTimerManager();

         _mixer->setReady(true);

         EventsBaseBackend::initBackend();
      }

      virtual void engineInit(){
         Common::String engineId = ConfMan.get("engineid");
         if ( engineId.equalsIgnoreCase("scumm") && ConfMan.getBool("original_gui") ){
            ConfMan.setBool("original_gui",false);
            log_cb(RETRO_LOG_INFO, "\"original_gui\" setting forced to false\n");
         }
      }

      virtual bool hasFeature(Feature f)
      {
         return (f == OSystem::kFeatureCursorPalette);
      }

      virtual void setFeatureState(Feature f, bool enable)
      {
         if (f == kFeatureCursorPalette)
            _mousePaletteEnabled = enable;
      }

      virtual bool getFeatureState(Feature f)
      {
         return (f == kFeatureCursorPalette) ? _mousePaletteEnabled : false;
      }

      virtual const GraphicsMode *getSupportedGraphicsModes() const
      {
         static const OSystem::GraphicsMode s_noGraphicsModes[] = { {0, 0, 0} };
         return s_noGraphicsModes;
      }

      virtual int getDefaultGraphicsMode() const
      {
         return 0;
      }

      virtual bool isOverlayVisible() const
      {
         return false;
      }

      virtual bool setGraphicsMode(int mode)
      {
         return true;
      }

      virtual int getGraphicsMode() const
      {
         return 0;
      }

      virtual void initSize(uint width, uint height, const Graphics::PixelFormat *format)
      {
         _gameScreen.create(width, height, format ? *format : Graphics::PixelFormat::createFormatCLUT8());
      }

      virtual int16 getHeight()
      {
         return _gameScreen.h;
      }

      virtual int16 getWidth()
      {
         return _gameScreen.w;
      }

      virtual Graphics::PixelFormat getScreenFormat() const
      {
         return _gameScreen.format;
      }

      virtual Common::List<Graphics::PixelFormat> getSupportedFormats() const
      {
         Common::List<Graphics::PixelFormat> result;

         /* RGBA8888 */
         result.push_back(Graphics::PixelFormat(4, 8, 8, 8, 8, 24, 16, 8, 0));

#ifdef FRONTEND_SUPPORTS_RGB565
         /* RGB565 - overlay */
         result.push_back(Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
#endif
         /* RGB555 - fmtowns */
         result.push_back(Graphics::PixelFormat(2, 5, 5, 5, 1, 10, 5, 0, 15));

         /* Palette - most games */
         result.push_back(Graphics::PixelFormat::createFormatCLUT8());
         return result;
      }



      virtual PaletteManager *getPaletteManager() { return this; }
   protected:
      // PaletteManager API
      virtual void setPalette(const byte *colors, uint start, uint num)
      {
         _gamePalette.set(colors, start, num);
      }

      virtual void grabPalette(byte *colors, uint start, uint num) const
      {
         _gamePalette.get(colors, start, num);
      }


   public:
      virtual void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h)
      {
         const uint8_t *src = (const uint8_t*)buf;
         uint8_t *pix = (uint8_t*)_gameScreen.pixels;
         copyRectToSurface(pix, _gameScreen.pitch, src, pitch, x, y, w, h, _gameScreen.format.bytesPerPixel);
      }

      virtual void updateScreen()
      {
         const Graphics::Surface& srcSurface = (_overlayInGUI) ? _overlay : _gameScreen;
         if(srcSurface.w && srcSurface.h)
         {
            switch(srcSurface.format.bytesPerPixel)
            {
               case 1:
               case 3:
                  blit_uint8_uint16_fast(_screen, srcSurface, _gamePalette);
                  break;
               case 2:
                  blit_uint16_uint16(_screen, srcSurface, _gamePalette);
                  break;
               case 4:
                  blit_uint32_uint16(_screen, srcSurface, _gamePalette);
                  break;
            }
         }

         // Draw Mouse
         if(_mouseVisible && _mouseImage.w && _mouseImage.h)
         {
            const int x = _mouseX - _mouseHotspotX;
            const int y = _mouseY - _mouseHotspotY;

            if(_mouseImage.format.bytesPerPixel == 1)
               blit_uint8_uint16(_screen, _mouseImage, x, y, _mousePaletteEnabled ? _mousePalette : _gamePalette, _mouseKeyColor);
            else
               blit_uint16_uint16(_screen, _mouseImage, x, y, _mousePaletteEnabled ? _mousePalette : _gamePalette, _mouseKeyColor);
         }
      }

      virtual Graphics::Surface *lockScreen()
      {
         return &_gameScreen;
      }

      virtual void unlockScreen()
      {
         /* EMPTY */
      }

      virtual void setShakePos(int shakeXOffset, int shakeYOffset)
      {
         // TODO
      }

      virtual void showOverlay(bool inGUI)
      {
        _overlayVisible = true;
        _overlayInGUI = inGUI;
      }

      virtual void hideOverlay()
      {
         _overlayInGUI = false;
      }

      virtual void clearOverlay()
      {
         _overlay.fillRect(Common::Rect(_overlay.w, _overlay.h), 0);
      }

      virtual void grabOverlay(Graphics::Surface &surface)
      {
         const unsigned char *src = (unsigned char*)_overlay.pixels;
         unsigned char *dst = (byte *)surface.getPixels();;
         unsigned i = RES_H_OVERLAY;

         do{
            memcpy(dst, src, RES_W_OVERLAY << 1);
            dst += surface.pitch;
            src += RES_W_OVERLAY << 1;
         }while(--i);
      }

      virtual void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h)
      {
         const uint8_t *src = (const uint8_t*)buf;
         uint8_t *pix = (uint8_t*)_overlay.pixels;
         copyRectToSurface(pix, _overlay.pitch, src, pitch, x, y, w, h, _overlay.format.bytesPerPixel);
      }

      virtual int16 getOverlayHeight()
      {
         return _overlay.h;
      }

      virtual int16 getOverlayWidth()
      {
         return _overlay.w;
      }

      virtual Graphics::PixelFormat getOverlayFormat() const
      {
         return _overlay.format;
      }

      virtual bool showMouse(bool visible)
      {
         const bool wasVisible = _mouseVisible;
         _mouseVisible = visible;
         return wasVisible;
      }

      virtual void warpMouse(int x, int y)
      {
         _mouseX = x;
         _mouseY = y;
      }

      virtual void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor = 255, bool dontScale = false, const Graphics::PixelFormat *format = NULL, const byte *mask = nullptr)
      {
         const Graphics::PixelFormat mformat = format ? *format : Graphics::PixelFormat::createFormatCLUT8();

         if(_mouseImage.w != w || _mouseImage.h != h || _mouseImage.format != mformat)
         {
            _mouseImage.create(w, h, mformat);
         }

         memcpy(_mouseImage.pixels, buf, h * _mouseImage.pitch);

         _mouseHotspotX = hotspotX;
         _mouseHotspotY = hotspotY;
         _mouseKeyColor = keycolor;
         _mouseDontScale = dontScale;
      }

      virtual void setCursorPalette(const byte *colors, uint start, uint num)
      {
         _mousePalette.set(colors, start, num);
         _mousePaletteEnabled = true;
      }
      
		void retroCheckThread(uint32 offset = 0)
      {
         if(_threadExitTime <= (getMillis() + offset))
         {
#if defined(USE_LIBCO)
            extern void retro_leave_thread();
            retro_leave_thread();
#else
            retro_switch_thread();
#endif
            _threadExitTime = getMillis() + 10;
         }
      }

      virtual bool pollEvent(Common::Event &event)
      {
         retroCheckThread();

         ((DefaultTimerManager*)_timerManager)->handler();


         if(!_events.empty())
         {
            event = _events.front();
            _events.pop_front();
            return true;
         }

         return false;
      }

      virtual uint32 getMillis(bool skipRecord = false)
      {
#if (defined(GEKKO) && !defined(WIIU))
         return (ticks_to_microsecs(gettime()) / 1000.0) - _startTime;
#elif defined(WIIU)
         return ((cpu_features_get_time_usec())/1000) - _startTime;
#elif defined(__CELLOS_LV2__)
         return (sys_time_get_system_time() / 1000.0) - _startTime;
#else
         struct timeval t;
         gettimeofday(&t, 0);

         return ((t.tv_sec * 1000) + (t.tv_usec / 1000)) - _startTime;
#endif
      }

      virtual void delayMillis(uint msecs)
      {
			// Implement 'non-blocking' sleep...
			uint32 start_time = getMillis();
			if (_speed_hack_enabled)
			{
				// Use janky inaccurate method...
				uint32 elapsed_time = 0;
				uint32 time_remaining = msecs;
				while(time_remaining > 0)
				{
					// If delay would take us past the next
					// thread exit time, exit the thread immediately
					// (i.e. start burning delay time in the main RetroArch
					// thread as soon as possible...)
					retroCheckThread(time_remaining);
					// Check how much delay time remains...
					elapsed_time = getMillis() - start_time;
					if (time_remaining > elapsed_time)
					{
						time_remaining = time_remaining - elapsed_time;
						usleep(1000);
					}
					else
					{
						time_remaining = 0;
					}
					// Have to handle the timer manager here, since some engines
					// (e.g. dreamweb) sit in a delayMillis() loop waiting for a
					// timer callback...
					((DefaultTimerManager*)_timerManager)->handler();
				}
			}
			else
			{
				// Use accurate method...
				while(getMillis() < start_time + msecs)
				{
					usleep(1000);
					retroCheckThread();
					// Have to handle the timer manager here, since some engines
					// (e.g. dreamweb) sit in a delayMillis() loop waiting for a
					// timer callback...
					((DefaultTimerManager*)_timerManager)->handler();
				}
			}
      }

      virtual Common::MutexInternal *createMutex(void)
      {
         return createLibretroMutexInternal();
      }

      virtual void quit()
      {
         // TODO:
      }

      virtual void addSysArchivesToSearchSet(Common::SearchSet &s, int priority = 0)
      {
         // TODO: NOTHING?
      }

      virtual void getTimeAndDate(TimeDate &t, bool skipRecord) const
      {
         time_t curTime = time(NULL);

#define YEAR0 1900
#define EPOCH_YR 1970
#define SECS_DAY (24L * 60L * 60L)
#define LEAPYEAR(year) (!((year) % 4) && (((year) % 100) || !((year) % 400)))
#define YEARSIZE(year) (LEAPYEAR(year) ? 366 : 365)
         const int _ytab[2][12] = {
            {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
            {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
         };
         int year = EPOCH_YR;
         unsigned long dayclock = (unsigned long)curTime % SECS_DAY;
         unsigned long dayno = (unsigned long)curTime / SECS_DAY;
         t.tm_sec = dayclock % 60;
         t.tm_min = (dayclock % 3600) / 60;
         t.tm_hour = dayclock / 3600;
         t.tm_wday = (dayno + 4) % 7; /* day 0 was a thursday */
         while (dayno >= YEARSIZE(year)) {
            dayno -= YEARSIZE(year);
            year++;
         }
         t.tm_year = year - YEAR0;
         t.tm_mon = 0;
         while (dayno >= _ytab[LEAPYEAR(year)][t.tm_mon]) {
            dayno -= _ytab[LEAPYEAR(year)][t.tm_mon];
            t.tm_mon++;
         }
         t.tm_mday = dayno + 1;
      }

      virtual Audio::Mixer *getMixer()
      {
         return _mixer;
      }

      virtual Common::String getDefaultConfigFileName()
      {
         return s_systemDir + "/scummvm.ini";
      }

      virtual void logMessage(LogMessageType::Type type, const char *message)
      {
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "%s\n", message);
      }


      //

      const Graphics::Surface& getScreen()
      {
         const Graphics::Surface& srcSurface = (_overlayInGUI) ? _overlay : _gameScreen;

         if(srcSurface.w != _screen.w || srcSurface.h != _screen.h)
         {
#ifdef FRONTEND_SUPPORTS_RGB565
            _screen.create(srcSurface.w, srcSurface.h, Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
#else
            _screen.create(srcSurface.w, srcSurface.h, Graphics::PixelFormat(2, 5, 5, 5, 1, 10, 5, 0, 15));
#endif
         }


         return _screen;
      }

#define ANALOG_RANGE 0x8000
#define BASE_CURSOR_SPEED 4
#define PI 3.141592653589793238

      void processMouse(retro_input_state_t aCallback, int device, float gampad_cursor_speed, float gamepad_acceleration_time, bool analog_response_is_quadratic, int analog_deadzone, float mouse_speed)
      {
         int16_t joy_x, joy_y, joy_rx, joy_ry, x, y;
         float analog_amplitude_x, analog_amplitude_y;
         int mouse_acc_int;
         bool do_joystick, do_mouse, down;
         float screen_adjusted_cursor_speed = (float)_screen.w / 320.0f; // Dpad cursor speed should always be based off a 320 wide screen, to keep speeds consistent
         float adjusted_cursor_speed = (float)BASE_CURSOR_SPEED * gampad_cursor_speed * screen_adjusted_cursor_speed;
		 float inverse_acceleration_time = (gamepad_acceleration_time > 0.0) ? (1.0 / 60.0) * (1.0 / gamepad_acceleration_time) : 1.0;
         int dpad_cursor_offset;
         double rs_radius, rs_angle;
         unsigned numpad_index;

         static const uint32_t retroButtons[2] = {RETRO_DEVICE_ID_MOUSE_LEFT, RETRO_DEVICE_ID_MOUSE_RIGHT};
         static const Common::EventType eventID[2][2] =
         {
            {Common::EVENT_LBUTTONDOWN, Common::EVENT_LBUTTONUP},
            {Common::EVENT_RBUTTONDOWN, Common::EVENT_RBUTTONUP}
         };
			
			static const unsigned gampad_key_map[8][4] = {
				{ RETRO_DEVICE_ID_JOYPAD_X,      (unsigned)Common::KEYCODE_ESCAPE,    (unsigned)Common::ASCII_ESCAPE,    0              }, // Esc
				{ RETRO_DEVICE_ID_JOYPAD_Y,      (unsigned)Common::KEYCODE_PERIOD,    46,                                0              }, // .
				{ RETRO_DEVICE_ID_JOYPAD_L,      (unsigned)Common::KEYCODE_RETURN,    (unsigned)Common::ASCII_RETURN,    0              }, // Enter
				{ RETRO_DEVICE_ID_JOYPAD_R,      (unsigned)Common::KEYCODE_KP5,       53,                                0              }, // Numpad 5
				{ RETRO_DEVICE_ID_JOYPAD_L2,     (unsigned)Common::KEYCODE_BACKSPACE, (unsigned)Common::ASCII_BACKSPACE, 0              }, // Backspace
				{ RETRO_DEVICE_ID_JOYPAD_L3,     (unsigned)Common::KEYCODE_F10,       (unsigned)Common::ASCII_F10,       0              }, // F10
				{ RETRO_DEVICE_ID_JOYPAD_R3,     (unsigned)Common::KEYCODE_KP0,       48,                                0              }, // Numpad 0
				{ RETRO_DEVICE_ID_JOYPAD_SELECT, (unsigned)Common::KEYCODE_F7,        (unsigned)Common::ASCII_F7,        RETROKMOD_CTRL }, // CTRL+F7 (virtual keyboard)
			};
			
			// Right stick circular wrap around: 1 -> 2 -> 3 -> 6 -> 9 -> 8 -> 7 -> 4
			static const unsigned gampad_numpad_map[8][2] = {
				{ (unsigned)Common::KEYCODE_KP1, 49 },
				{ (unsigned)Common::KEYCODE_KP2, 50 },
				{ (unsigned)Common::KEYCODE_KP3, 51 },
				{ (unsigned)Common::KEYCODE_KP6, 54 },
				{ (unsigned)Common::KEYCODE_KP9, 57 },
				{ (unsigned)Common::KEYCODE_KP8, 56 },
				{ (unsigned)Common::KEYCODE_KP7, 55 },
				{ (unsigned)Common::KEYCODE_KP4, 52 },
			};
			
			// Reduce gamepad cursor speed, if required
			if (device == RETRO_DEVICE_JOYPAD &&
			    aCallback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2))
			{
				adjusted_cursor_speed = adjusted_cursor_speed * (1.0f / 3.0f);
			}

         down = false;
         do_joystick = false;
         x = aCallback(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
         y = aCallback(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
         joy_x = aCallback(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
         joy_y = aCallback(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
			
			// Left Analog X Axis
			if (joy_x > analog_deadzone || joy_x < -analog_deadzone)
			{
				if (joy_x > analog_deadzone)
				{
					// Reset accumulator when changing direction
					_mouseXAcc = (_mouseXAcc < 0.0) ? 0.0 : _mouseXAcc;
					joy_x = joy_x - analog_deadzone;
				}
				if (joy_x < -analog_deadzone)
				{
					// Reset accumulator when changing direction
					_mouseXAcc = (_mouseXAcc > 0.0) ? 0.0 : _mouseXAcc;
					joy_x = joy_x + analog_deadzone;
				}
				// Update accumulator
				analog_amplitude_x = (float)joy_x / (float)(ANALOG_RANGE - analog_deadzone);
				if (analog_response_is_quadratic)
				{
					if (analog_amplitude_x < 0.0)
						analog_amplitude_x = -(analog_amplitude_x * analog_amplitude_x);
					else
						analog_amplitude_x = analog_amplitude_x * analog_amplitude_x;
				}
				//printf("analog_amplitude_x: %f\n", analog_amplitude_x);
				_mouseXAcc += analog_amplitude_x * adjusted_cursor_speed;
				// Get integer part of accumulator
				mouse_acc_int = (int)_mouseXAcc;
				if (mouse_acc_int != 0)
				{
					// Set mouse position
					_mouseX += mouse_acc_int;
					_mouseX = (_mouseX < 0) ? 0 : _mouseX;
					_mouseX = (_mouseX >= _screen.w) ? _screen.w : _mouseX;
					do_joystick = true;
					// Update accumulator
					_mouseXAcc -= (float)mouse_acc_int;
				}
			}
			
			// Left Analog Y Axis
			if (joy_y > analog_deadzone || joy_y < -analog_deadzone)
			{
				if (joy_y > analog_deadzone)
				{
					// Reset accumulator when changing direction
					_mouseYAcc = (_mouseYAcc < 0.0) ? 0.0 : _mouseYAcc;
					joy_y = joy_y - analog_deadzone;
				}
				if (joy_y < -analog_deadzone)
				{
					// Reset accumulator when changing direction
					_mouseYAcc = (_mouseYAcc > 0.0) ? 0.0 : _mouseYAcc;
					joy_y = joy_y + analog_deadzone;
				}
				// Update accumulator
				analog_amplitude_y = (float)joy_y / (float)(ANALOG_RANGE - analog_deadzone);
				if (analog_response_is_quadratic)
				{
					if (analog_amplitude_y < 0.0)
						analog_amplitude_y = -(analog_amplitude_y * analog_amplitude_y);
					else
						analog_amplitude_y = analog_amplitude_y * analog_amplitude_y;
				}
				//printf("analog_amplitude_y: %f\n", analog_amplitude_y);
				_mouseYAcc += analog_amplitude_y * adjusted_cursor_speed;
				// Get integer part of accumulator
				mouse_acc_int = (int)_mouseYAcc;
				if (mouse_acc_int != 0)
				{
					// Set mouse position
					_mouseY += mouse_acc_int;
					_mouseY = (_mouseY < 0) ? 0 : _mouseY;
					_mouseY = (_mouseY >= _screen.h) ? _screen.h : _mouseY;
					do_joystick = true;
					// Update accumulator
					_mouseYAcc -= (float)mouse_acc_int;
				}
			}

         if (device == RETRO_DEVICE_JOYPAD)
         {
            bool dpadLeft  = aCallback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
            bool dpadRight = aCallback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
            bool dpadUp    = aCallback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
            bool dpadDown  = aCallback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);

            if (dpadLeft || dpadRight)
            {
               _dpadXVel = MIN(_dpadXVel + inverse_acceleration_time, 1.0f);
            }
            else
            {
               _dpadXVel = 0.0f;
            }

            if (dpadUp || dpadDown)
            {
               _dpadYVel = MIN(_dpadYVel + inverse_acceleration_time, 1.0f);
            }
            else
            {
               _dpadYVel = 0.0f;
            }

            if (dpadLeft)
            {
               _dpadXAcc = MIN(_dpadXAcc - _dpadXVel * adjusted_cursor_speed, 0.0f);
               _mouseX += (int)_dpadXAcc;
               _dpadXAcc -= (float)(int)_dpadXAcc;

               _mouseX = (_mouseX < 0) ? 0 : _mouseX;
               _mouseX = (_mouseX >= _screen.w) ? _screen.w : _mouseX;
               do_joystick = true;
            }
            if (dpadRight)
            {
               _dpadXAcc = MAX(_dpadXAcc + _dpadXVel * adjusted_cursor_speed, 0.0f);
               _mouseX += (int)_dpadXAcc;
               _dpadXAcc -= (float)(int)_dpadXAcc;

               _mouseX = (_mouseX < 0) ? 0 : _mouseX;
               _mouseX = (_mouseX >= _screen.w) ? _screen.w : _mouseX;
               do_joystick = true;
            }

            if (dpadUp)
            {
               _dpadYAcc = MIN(_dpadYAcc - _dpadYVel * adjusted_cursor_speed, 0.0f);
               _mouseY += (int)_dpadYAcc;
               _dpadYAcc -= (float)(int)_dpadYAcc;

               _mouseY = (_mouseY < 0) ? 0 : _mouseY;
               _mouseY = (_mouseY >= _screen.h) ? _screen.h : _mouseY;
               do_joystick = true;
            }

            if (dpadDown)
            {
               _dpadYAcc = MAX(_dpadYAcc + _dpadYVel * adjusted_cursor_speed, 0.0f);
               _mouseY += (int)_dpadYAcc;
               _dpadYAcc -= (float)(int)_dpadYAcc;

               _mouseY = (_mouseY < 0) ? 0 : _mouseY;
               _mouseY = (_mouseY >= _screen.h) ? _screen.h : _mouseY;
               do_joystick = true;
            }

            if (aCallback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START))
            {
               Common::Event ev;
               ev.type = Common::EVENT_MAINMENU;
               _events.push_back(ev);
            }
         }

#if defined(WIIU) || defined(__SWITCH__)
	int p_x = aCallback(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
	int p_y = aCallback(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
	int p_press  = aCallback(0, RETRO_DEVICE_POINTER, 0,RETRO_DEVICE_ID_POINTER_PRESSED);
	int px=(int)((p_x+0x7fff)*_screen.w /0xffff);
	int py=(int)((p_y+0x7fff)*_screen.h/0xffff);
	//printf("(%d,%d) p:%d\n",px,py,pp);

	static int ptrhold=0;

	if(p_press)ptrhold++;
	else ptrhold=0;

	if(ptrhold>0){
	   _mouseX = px;
	   _mouseY = py;

            Common::Event ev;
            ev.type = Common::EVENT_MOUSEMOVE;
            ev.mouse.x = _mouseX;
            ev.mouse.y = _mouseY;
            _events.push_back(ev);
	}

	if(ptrhold>10 && _ptrmouseButton==0){
	    _ptrmouseButton=1;
            Common::Event ev;
            ev.type = eventID[0][_ptrmouseButton ? 0 : 1];
            ev.mouse.x = _mouseX;
            ev.mouse.y = _mouseY;
            _events.push_back(ev);
	}
	else if (ptrhold==0 && _ptrmouseButton==1){
	    _ptrmouseButton=0;
            Common::Event ev;
            ev.type = eventID[0][_ptrmouseButton ? 0 : 1];
            ev.mouse.x = _mouseX;
            ev.mouse.y = _mouseY;
            _events.push_back(ev);
	}

#endif

         if (do_joystick)
         {
            Common::Event ev;
            ev.type = Common::EVENT_MOUSEMOVE;
            ev.mouse.x = _mouseX;
            ev.mouse.y = _mouseY;
            _events.push_back(ev);
         }

         // Gampad mouse buttons
         down = aCallback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
         if(down != _joypadmouseButtons[0])
         {
            _joypadmouseButtons[0] = down;

            Common::Event ev;
            ev.type = eventID[0][down ? 0 : 1];
            ev.mouse.x = _mouseX;
            ev.mouse.y = _mouseY;
            _events.push_back(ev);
         }

         down = aCallback(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
         if(down != _joypadmouseButtons[1])
         {
            _joypadmouseButtons[1] = down;

            Common::Event ev;
            ev.type = eventID[1][down ? 0 : 1];
            ev.mouse.x = _mouseX;
            ev.mouse.y = _mouseY;
            _events.push_back(ev);
         }

			// Gamepad keyboard buttons
			for(int i = 0; i < 8; i ++)
			{
				down = aCallback(0, RETRO_DEVICE_JOYPAD, 0, gampad_key_map[i][0]);
				if (down != _joypadkeyboardButtons[i])
				{
					_joypadkeyboardButtons[i] = down;
					bool state = down ? true : false;
					processKeyEvent(state, gampad_key_map[i][1], (uint32_t)gampad_key_map[i][2], (uint32_t)gampad_key_map[i][3]);
				}
			}
			
			// Gamepad right stick numpad emulation
			joy_rx = aCallback(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
			joy_ry = aCallback(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
			
			if (joy_rx > analog_deadzone)
				joy_rx = joy_rx - analog_deadzone;
			else if (joy_rx < -analog_deadzone)
				joy_rx = joy_rx + analog_deadzone;
			else
				joy_rx = 0;
			
			if (joy_ry > analog_deadzone)
				joy_ry = joy_ry - analog_deadzone;
			else if (joy_ry < -analog_deadzone)
				joy_ry = joy_ry + analog_deadzone;
			else
				joy_ry = 0;
			
			// This is very ugly, but I don't have time to make it nicer...
			if (joy_rx != 0 || joy_ry != 0)
			{
				analog_amplitude_x = (float)joy_rx / (float)(ANALOG_RANGE - analog_deadzone);
				analog_amplitude_y = (float)joy_ry / (float)(ANALOG_RANGE - analog_deadzone);
				
				// Convert to polar coordinates: part 1
				rs_radius = sqrt((double)(analog_amplitude_x * analog_amplitude_x) + (double)(analog_amplitude_y * analog_amplitude_y));
				
				// Check if radius is above threshold
				if (rs_radius > 0.5)
				{
					// Convert to polar coordinates: part 2
					rs_angle = atan2((double)analog_amplitude_y, (double)analog_amplitude_x);
					
					// Adjust rotation offset...
					rs_angle = (2.0 * PI) - (rs_angle + PI);
					rs_angle = fmod(rs_angle - (0.125 * PI), 2.0 * PI);
					if (rs_angle < 0)
						rs_angle += 2.0 * PI;

					// Convert angle into numpad key index
					numpad_index = (unsigned)((rs_angle / (2.0 * PI)) * 8.0);
					// Unnecessary safety check...
					numpad_index = (numpad_index > 7) ? 7 : numpad_index;
					//printf("numpad_index: %u\n", numpad_index);
					
					if (numpad_index != _joypadnumpadLast)
					{
						// Unset last key, if required
						if (_joypadnumpadActive)
							processKeyEvent(false, gampad_numpad_map[_joypadnumpadLast][0], (uint32_t)gampad_numpad_map[_joypadnumpadLast][1], 0);
						
						// Set new key
						processKeyEvent(true, gampad_numpad_map[numpad_index][0], (uint32_t)gampad_numpad_map[numpad_index][1], 0);
						
						_joypadnumpadLast = numpad_index;
						_joypadnumpadActive = true;
					}
				}
				else if (_joypadnumpadActive)
				{
					processKeyEvent(false, gampad_numpad_map[_joypadnumpadLast][0], (uint32_t)gampad_numpad_map[_joypadnumpadLast][1], 0);
					_joypadnumpadActive = false;
					_joypadnumpadLast = 8;
				}
			}
			else if (_joypadnumpadActive)
			{
				processKeyEvent(false, gampad_numpad_map[_joypadnumpadLast][0], (uint32_t)gampad_numpad_map[_joypadnumpadLast][1], 0);
				_joypadnumpadActive = false;
				_joypadnumpadLast = 8;
			}
         
         // Process input from physical mouse
         do_mouse = false;
         // > X Axis
         if (x != 0)
         {
            if (x > 0) {
               // Reset accumulator when changing direction
               _mouseXAcc = (_mouseXAcc < 0.0) ? 0.0 : _mouseXAcc;
            }
            if (x < 0) {
               // Reset accumulator when changing direction
               _mouseXAcc = (_mouseXAcc > 0.0) ? 0.0 : _mouseXAcc;
            }
            // Update accumulator
            _mouseXAcc += (float)x * mouse_speed;
            // Get integer part of accumulator
            mouse_acc_int = (int)_mouseXAcc;
            if (mouse_acc_int != 0)
            {
               // Set mouse position
               _mouseX += mouse_acc_int;
               _mouseX = (_mouseX < 0) ? 0 : _mouseX;
               _mouseX = (_mouseX >= _screen.w) ? _screen.w : _mouseX;
               do_mouse = true;
               // Update accumulator
               _mouseXAcc -= (float)mouse_acc_int;
            }
         }
         // > Y Axis
         if (y != 0)
         {
            if (y > 0) {
               // Reset accumulator when changing direction
               _mouseYAcc = (_mouseYAcc < 0.0) ? 0.0 : _mouseYAcc;
            }
            if (y < 0) {
               // Reset accumulator when changing direction
               _mouseYAcc = (_mouseYAcc > 0.0) ? 0.0 : _mouseYAcc;
            }
            // Update accumulator
            _mouseYAcc += (float)y * mouse_speed;
            // Get integer part of accumulator
            mouse_acc_int = (int)_mouseYAcc;
            if (mouse_acc_int != 0)
            {
               // Set mouse position
               _mouseY += mouse_acc_int;
               _mouseY = (_mouseY < 0) ? 0 : _mouseY;
               _mouseY = (_mouseY >= _screen.h) ? _screen.h : _mouseY;
               do_mouse = true;
               // Update accumulator
               _mouseYAcc -= (float)mouse_acc_int;
            }
         }

         if (do_mouse)
         {
            Common::Event ev;
            ev.type = Common::EVENT_MOUSEMOVE;
            ev.mouse.x = _mouseX;
            ev.mouse.y = _mouseY;
            _events.push_back(ev);
         }

         for(int i = 0; i < 2; i ++)
         {
            Common::Event ev;
            bool down = aCallback(0, RETRO_DEVICE_MOUSE, 0, retroButtons[i]);
            if(down != _mouseButtons[i])
            {
               _mouseButtons[i] = down;

               ev.type = eventID[i][down ? 0 : 1];
               ev.mouse.x = _mouseX;
               ev.mouse.y = _mouseY;
               _events.push_back(ev);
            }

         }
      }

      void processKeyEvent(bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers)
      {
         int _keyflags = 0;
         _keyflags |= (key_modifiers & RETROKMOD_CTRL) ? Common::KBD_CTRL : 0;
         _keyflags |= (key_modifiers & RETROKMOD_ALT) ? Common::KBD_ALT : 0;
         _keyflags |= (key_modifiers & RETROKMOD_SHIFT) ? Common::KBD_SHIFT : 0;
         _keyflags |= (key_modifiers & RETROKMOD_META) ? Common::KBD_META : 0;
         _keyflags |= (key_modifiers & RETROKMOD_CAPSLOCK) ? Common::KBD_CAPS : 0;
         _keyflags |= (key_modifiers & RETROKMOD_NUMLOCK) ? Common::KBD_NUM : 0;
         _keyflags |= (key_modifiers & RETROKMOD_SCROLLOCK) ? Common::KBD_SCRL : 0;

         Common::Event ev;
         ev.type = down ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
         ev.kbd.keycode = (Common::KeyCode)keycode;
         ev.kbd.flags = _keyflags;
         ev.kbd.ascii = keycode;

         /* If shift was down then send upper case letter to engine */
         if(ev.kbd.ascii >= 97 && ev.kbd.ascii <= 122 && (_keyflags & Common::KBD_SHIFT))
            ev.kbd.ascii = ev.kbd.ascii & ~0x20;

         _events.push_back(ev);
      }

      void postQuit()
      {
         Common::Event ev;
         ev.type = Common::EVENT_QUIT;
         dynamic_cast<OSystem_RETRO *>(g_system)->getEventManager()->pushEvent(ev);
      }
};

OSystem* retroBuildOS(bool aEnableSpeedHack)
{
   return new OSystem_RETRO(aEnableSpeedHack);
}

const Graphics::Surface& getScreen()
{
   return dynamic_cast<OSystem_RETRO *>(g_system)->getScreen();
}

void retroProcessMouse(retro_input_state_t aCallback, int device, float gamepad_cursor_speed, float gamepad_acceleration_time, bool analog_response_is_quadratic, int analog_deadzone, float mouse_speed)
{
   dynamic_cast<OSystem_RETRO *>(g_system)->processMouse(aCallback, device, gamepad_cursor_speed, gamepad_acceleration_time, analog_response_is_quadratic, analog_deadzone, mouse_speed);
}

void retroPostQuit()
{
   dynamic_cast<OSystem_RETRO *>(g_system)->postQuit();
}

void retroSetSystemDir(const char* aPath)
{
   s_systemDir = Common::String(aPath ? aPath : ".");
}

void retroSetSaveDir(const char* aPath)
{
   s_saveDir = Common::String(aPath ? aPath : ".");
}

void retroKeyEvent(bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers)
{
   dynamic_cast<OSystem_RETRO *>(g_system)->processKeyEvent(down, keycode, character, key_modifiers);
}
