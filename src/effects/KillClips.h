/**********************************************************************

  Audacity: A Digital Audio Editor

  Fade.h

  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_EFFECT_KILLCLIPS__
#define __AUDACITY_EFFECT_KILLCLIPS__

#include <vector>
#include <map>
#include <wx/string.h>

#include "Effect.h"

#define KILLCLIPS_PLUGIN_SYMBOL XO("Kill Clips")

class EffectKillClips final : public Effect
{
public:
   EffectKillClips();
   virtual ~EffectKillClips();

   // IdentInterface implementation

   wxString GetSymbol() override;
   wxString GetDescription() override;
   wxString ManualPage() override;

   // EffectIdentInterface implementation

   bool GetAutomationParameters(EffectAutomationParameters & parms);
   bool SetAutomationParameters(EffectAutomationParameters & parms);
   void PopulateOrExchange(ShuttleGui & S) override;
   bool TransferDataToWindow() override;
   bool TransferDataFromWindow() override;

   EffectType GetType() override;
   bool IsInteractive() override;

   // EffectClientInterface implementation

   unsigned GetAudioInCount() override;
   unsigned GetAudioOutCount() override;
   bool ProcessInitialize(sampleCount totalLen, ChannelNames chanMap = NULL) override;

// new:
   bool Process() override;
   bool ProcessOne(WaveTrack * t, const wxString &msg, int curTrackNum);
   bool AnalyseTrack(const WaveTrack * track, const wxString &msg,
                  int curTrackNum);

private:
   // EffectKillClipsIn implementation

   enum state_type
   {
      state_normal,
      state_fade_out,
      state_silence,
      state_fade_in
   };

   void DoFade(float *ibuf, unsigned long long len, sampleCount silence_len,
               float reduction);

   void OnText(wxCommandEvent & evt);

   struct silence_t {
      sampleCount start;
      sampleCount length;
      float reduction;
      silence_t(sampleCount start, sampleCount length, float reduction) :
         start(start), length(length), reduction(reduction) {}
   };

   std::map<int, std::vector<silence_t>> silence;

   float mMax, mMin, mHigh;
   int mSlowness;

   double mCurT0;
   double mCurT1;
   int curOverMax;

   // analyze
   sampleCount okRemain;
   sampleCount lowCounted;
   sampleCount insertCount;
   sampleCount oldPos;
   sampleCount insertPos;

   // process
   sampleCount mSample;
   sampleCount to_write;
   state_type last_state;

   DECLARE_EVENT_TABLE()
};

#endif
