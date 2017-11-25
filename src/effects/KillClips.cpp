/**********************************************************************

  Audacity: A Digital Audio Editor

  Fade.cpp

  Robert Leidle

*******************************************************************//**

\class EffectKillClips
\brief An Effect that reduces the volume to zero over  achosen interval.

*//*******************************************************************/

#include "../Audacity.h"

#include <wx/intl.h>
#include <wx/valgen.h>

#include "../ShuttleGui.h"
#include "../widgets/valnum.h"
#include "../WaveTrack.h"

#include "KillClips.h"

// Define keys, defaults, minimums, and maximums for the effect parameters
//
//     Name    Type     Key                     Def      Min      Max      Scale
Param( Amount, float,   XO("Percentage"),       0.5,     0.0,     1.0,     0.1);

BEGIN_EVENT_TABLE(EffectKillClips, wxEvtHandler)
    EVT_TEXT(wxID_ANY, EffectKillClips::OnText)
END_EVENT_TABLE()

EffectKillClips::EffectKillClips()
{
}

EffectKillClips::~EffectKillClips()
{
}

// IdentInterface implementation

wxString EffectKillClips::GetSymbol()
{
   return KILLCLIPS_PLUGIN_SYMBOL;
}

wxString EffectKillClips::GetDescription()
{
   return "Silences clips, fades in and out around that";
}

wxString EffectKillClips::ManualPage()
{
   return wxT("EffectKillClips");
}

// EffectIdentInterface implementation

bool EffectKillClips::GetAutomationParameters(EffectAutomationParameters & parms)
{
   parms.WriteFloat(KEY_Amount, mAmount);

   return true;
}

bool EffectKillClips::SetAutomationParameters(EffectAutomationParameters & parms)
{
   ReadAndVerifyFloat(Amount);

   mAmount = Amount;

   return true;
}

void EffectKillClips::PopulateOrExchange(ShuttleGui &S)
{
   S.StartMultiColumn(1, wxALIGN_CENTER);
   {
      FloatingPointValidator<float> vldAmount(1, &mAmount);
      vldAmount.SetMin(MIN_Amount);

      /* i18n-hint: This is how many times longer the sound will be, e.g. applying
       * the effect to a 1-second sample, with the default Stretch Factor of 10.0
       * will give an (approximately) 10 second sound
       */
      S.AddTextBox(_("Stretch Factor:"), wxT(""), 10)->SetValidator(vldAmount);
   }
   S.EndMultiColumn();
}

bool EffectKillClips::TransferDataToWindow()
{
   if (!mUIParent->TransferDataToWindow())
   {
      return false;
   }

   return true;
}

bool EffectKillClips::TransferDataFromWindow()
{
   if (!mUIParent->Validate() || !mUIParent->TransferDataFromWindow())
   {
      return false;
   }

   return true;
}

void EffectKillClips::OnText(wxCommandEvent & WXUNUSED(evt))
{
   EnableApply(mUIParent->TransferDataFromWindow());
}

EffectType EffectKillClips::GetType()
{
   return EffectTypeProcess;
}

bool EffectKillClips::IsInteractive()
{
   return true;
}

// EffectClientInterface implementation

unsigned EffectKillClips::GetAudioInCount()
{
   return 1;
}

unsigned EffectKillClips::GetAudioOutCount()
{
   return 1;
}

const sampleCount incsize = 16384;

bool EffectKillClips::ProcessInitialize(sampleCount WXUNUSED(totalLen), ChannelNames WXUNUSED(chanMap))
{
   return true;
}

void EffectKillClips::DoFade(float *ibuf, unsigned long long len, sampleCount silence_len)
{
   if(last_state == state_normal || last_state == state_fade_out)
   {
      last_state = state_fade_out;
      for (; (mSample.as_long_long() > 0) && len; --len, ++ibuf)
      {
         *ibuf *= ( mSample-- ).as_float() / incsize.as_float();
      }
      if(!mSample.as_long_long()) {
         to_write = silence_len;
         last_state = state_silence;
      }
   }
   if(last_state == state_silence)
   {
      assert(to_write.as_long_long());
      for (; (to_write.as_long_long() > 0) && len; --len, ++ibuf, --to_write)
      {
         *ibuf = 0.0f;
      }
      if(!to_write.as_long_long())
         last_state = state_fade_in;
   }
   if(last_state == state_fade_in)
   {
      for (; (mSample < incsize) && len; --len, ++ibuf)
      {
         *ibuf *= ( mSample++ ).as_float() / incsize.as_float();
      }
      if(mSample == incsize)
         last_state = state_normal;
   }
// printf("STATE: %d\n", last_state);
}

//#include <iostream>

///////////////////////

bool EffectKillClips::ProcessOne(
   WaveTrack * track, const wxString &msg, int curTrackNum, float offset)
{
   bool rc = true;

   //Transform the marker timepoints to samples
   auto start = track->TimeToLongSamples(mCurT0);
   auto end = track->TimeToLongSamples(mCurT1);

   //Get the length of the buffer (as double). len is
   //used simply to calculate a progress meter, so it is easier
   //to make it a double now than it is to do it later
   auto len = (end - start).as_double();

   //Initiate a processing buffer.  This buffer will (most likely)
   //be shorter than the length of the track being processed.
   Floats buffer{ track->GetMaxBlockSize() };

   std::vector<silence_t>::const_iterator itr = silence[curTrackNum].begin();

   // initialize variables
   okRemain = 0;
   if(silence[curTrackNum].size() && itr->start - start < incsize)
   {
      mSample = itr->start - start; // mSample will be 0 at itr->start
      last_state = state_fade_out;
   }
   else
   {
      mSample = incsize;
      last_state = state_normal;
   }

   //Go through the track one buffer at a time. s counts which
   //sample the current buffer starts at.
   auto s = start;
   while (itr != silence[curTrackNum].end() && s < end) {
      //Get a block of samples (smaller than the size of the buffer)
      //Adjust the block size if it is the final block in the track
      const auto block = limitSampleBufferSize(
         track->GetBestBlockSize(s),
         end - s
      );

      //Get the samples from the track and put them in the buffer
      track->Get((samplePtr) buffer.get(), floatSample, s, block);

      //Process the buffer (stored in variable "buffer" now)

      // not yet finished over block border?
      if(last_state != state_normal)
      {
         DoFade(buffer.get(), block, itr->length);
         if(last_state == state_normal) // silence has been finished this block
            ++itr;
      }

      // any further silence in this block?
      //printf("last round: buffer=%lld, last_state=%d, it->start=%llu, s=%lld, block=%lld, itr->length=%lld\n",
      //       (long long)buffer.get(), last_state, itr->start.as_long_long(), s.as_long_long(), block, itr->length.as_long_long());
      while(itr != silence[curTrackNum].end() && last_state == state_normal && itr->start - incsize < s+block)
      {
         // printf("START,LEN: %llu, %llu\n", itr->start.as_long_long(), itr->length.as_long_long());

         // this assertion is usually true due to the while loop condition:
         //     itr->start - incsize      >= s + block (last round)
         // <=> -s + itr->start - incsize >= block (last round) = 0 (this round)
         //     -s + itr->start - incsize >= 0
         assert((-s.as_long_long() + itr->start.as_long_long() - incsize.as_long_long()) >= 0);

         DoFade(buffer.get() - s.as_long_long() + itr->start.as_long_long() - incsize.as_long_long(),
                block - itr->start.as_long_long() + s.as_long_long() + incsize.as_long_long(),
                itr->length);
         if(last_state == state_normal) // silence has been finished this block
            ++itr;

         //printf("WHILE: ok: %d, last round: buffer=%lld, last_state=%d, it->start=%llu, s=%lld, block=%lld, itr->length=%lld\n",
         //        itr != silence[curTrackNum].end(),
         //        (long long)buffer.get(), last_state, itr->start.as_long_long(), s.as_long_long(), block, itr->length.as_long_long());
      }

      // The algorithm makes sure that everything is below mAmount, so normalize
      const float mult = 1.0f / mAmount;
      for(sampleCount i = 0; i < block; ++i)
      {
         buffer.get()[i.as_long_long()] *= mult;
         assert(buffer.get()[i.as_long_long()] <= 1.0f);
      }

      //Copy the newly-changed samples back onto the track.
      track->Set((samplePtr) buffer.get(), floatSample, s, block);

      //Increment s one blockfull of samples
      s += block;

      //Update the Progress meter
      if (TrackProgress(curTrackNum,
                        0.5+((s - start).as_double() / len)/2.0, msg)) {
         rc = false; //lda .. break, not return, so that buffer is deleted
         break;
      }
   }

   //Return true because the effect processing succeeded ... unless cancelled
   return rc;
}

bool EffectKillClips::AnalyseTrack(const WaveTrack * track, const wxString &msg,
                                   int curTrackNum,
                                   float &offset, float &min, float &max)
{
   // Since we need complete summary data, we need to block until the OD tasks are done for this track
   // TODO: should we restrict the flags to just the relevant block files (for selections)
   while (track->GetODFlags()) {
      // update the gui
      if (ProgressResult::Cancelled == mProgress->Update(
         0, wxT("Waiting for waveform to finish computing...")) )
         return false;
      wxMilliSleep(100);
   }

   //Transform the marker timepoints to samples
   auto start = track->TimeToLongSamples(mCurT0);
   auto end = track->TimeToLongSamples(mCurT1);

   //Initiate a processing buffer.  This buffer will (most likely)
   //be shorter than the length of the track being processed.
   Floats buffer{ track->GetMaxBlockSize() };

   //Go through the track one buffer at a time. s counts which
   //sample the current buffer starts at.
   auto s = start;
   while (s < end) {
      //Get a block of samples (smaller than the size of the buffer)
      //Adjust the block size if it is the final block in the track
      const auto block = limitSampleBufferSize(
         track->GetBestBlockSize(s),
         end - s
      );

      //Get the samples from the track and put them in the buffer
      track->Get((samplePtr) buffer.get(), floatSample, s, block);
      float* mbuffer = buffer.get();

      decltype(s) blockEnd = s + block;
      for(decltype(s) pos = 0; pos < block;)
      {
         if((!okRemain.as_long_long()) && fabs(mbuffer[pos.as_long_long()]) > mAmount)
         {
            okRemain = incsize * 2;
            oldPos = s + pos;
         }
         if(okRemain.as_long_long())
         {
            for( ; (pos < block) && okRemain.as_long_long(); ++pos)
            {
               if(fabs(mbuffer[pos.as_long_long()]) > mAmount)
               {
                  okRemain = incsize * 2;
               }
               else
                  --okRemain;
            }

            if(!okRemain.as_long_long())
            {
               silence[curTrackNum].emplace_back(oldPos, s + pos - oldPos - ( 2 * incsize));
               /*printf("emplacing silence:     start: %lld (%lf), length: %lld\n",
                      oldPos.as_long_long(),
                      track->LongSamplesToTime(oldPos),
                      (s + pos - oldPos - ( 2 * incsize)).as_long_long());*/
            }
         }
         else
            ++pos;
      }

      //Increment s one blockfull of samples
      s = blockEnd;

/*      //Update the Progress meter
      if (TrackProgress(curTrackNum,
                        0.5+((s - start).as_double() / len)/2.0, msg)) {
         rc = false; //lda .. break, not return, so that buffer is deleted
         break;
      }*/
   }

   return true;
}

bool EffectKillClips::Process()
{
   // this should go to the initialize function, but it's not working...
   silence.clear();

   //Iterate over each track
   this->CopyInputTracks(); // Set up mOutputTracks.
   bool bGoodResult = true;
   SelectedTrackListOfKindIterator iter(Track::Wave, mOutputTracks.get());
   WaveTrack *track = (WaveTrack *) iter.First();
   WaveTrack *prevTrack;
   prevTrack = track;
   int curTrackNum = 0;
   wxString topMsg;
   topMsg = _("Killing Clips...\n");

   while (track) {
      //Get start and end times from track
      double trackStart = track->GetStartTime();
      double trackEnd = track->GetEndTime();

      //Set the current bounds to whichever left marker is
      //greater and whichever right marker is less:
      mCurT0 = mT0 < trackStart? trackStart: mT0;
      mCurT1 = mT1 > trackEnd? trackEnd: mT1;

      // Process only if the right marker is to the right of the left marker
      if (mCurT1 > mCurT0) {
         wxString msg;
         wxString trackName = track->GetName();


         msg = topMsg + _("Analyzing Clips: ") + trackName;

         float offset, min, max;
         if (! ( bGoodResult =
                 AnalyseTrack(track, msg, curTrackNum, offset, min, max) ) )
             break;
         if(!track->GetLinked()) {
            // mono or 'stereo tracks independently'
            msg = topMsg + _("Processing: ") + trackName;
            if(track->GetLinked() || prevTrack->GetLinked())  // only get here if there is a linked track but we are processing independently
               msg = topMsg + _("Processing stereo channels independently: ") + trackName;

            if (!ProcessOne(track, msg, curTrackNum, offset))
            {
               bGoodResult = false;
               break;
            }
         }
         else
         {
            // we have a linked stereo track
            // so we need to find it's min, max and offset
            // as they are needed to calc the multiplier for both tracks
            track = (WaveTrack *) iter.Next();  // get the next one
            msg = topMsg + _("Analyzing second track of stereo pair: ") + trackName;
            float offset2, min2, max2;
            if ( ! ( bGoodResult =
                     AnalyseTrack(track, msg, curTrackNum + 1, offset2, min2, max2) ) )
                break;

            track = (WaveTrack *) iter.Prev();  // go back to the first linked one
            msg = topMsg + _("Processing first track of stereo pair: ") + trackName;
            if (!ProcessOne(track, msg, curTrackNum, offset))
            {
               bGoodResult = false;
               break;
            }
            track = (WaveTrack *) iter.Next();  // go to the second linked one
            curTrackNum++;   // keeps progress bar correct
            msg = topMsg + _("Processing second track of stereo pair: ") + trackName;
            if (!ProcessOne(track, msg, curTrackNum, offset2))
            {
               bGoodResult = false;
               break;
            }
         }
      }

      //Iterate to the next track
      prevTrack = track;
      track = (WaveTrack *) iter.Next();
      curTrackNum++;
   }

   this->ReplaceProcessedTracks(bGoodResult);
   return bGoodResult;
}


