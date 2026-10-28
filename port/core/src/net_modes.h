// What the relay session's files share for the session's kinds besides a live race (playback.cpp, rejoin.cpp,
// practice_session.cpp): split from net_internal.h, which includes it at its end.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "net_internal.h"

#include <array>
#include <string>

namespace fzgx::net
{
// playback.cpp
void SpectateReplay(Guest& g, Session& s, u32 step);
void VerifyReplay(Guest& g, Session& s);
void ReportResultsPictures(Guest& g, const char* when);
void ReportPresentation(Guest& g, Session& s);
void PlaybackSummary(Guest& g, Session& s);
bool StartPlayback(Guest& g, const std::string& path);

// spectator_frame.cpp: the advanced spectator controls in a replay played as the race (fzgx/spectator.h): a
// displayed frame's choices into the game's cameras, after SpectateReplay; their line at the end.
void SpectatorFrame(Guest& g, Session& s, u32 step, u32 module);
void SpectatorSummary(const Session& s);
// observer_frame.cpp: the observer HUD's numbers added to what the overlay is told (a displayed frame's, at `step`);
// the game's own HUD hidden behind it in every view but the game's, put back for the game's (followed_races: the
// racer followed still races) and, with the overlay told to go (racing: the local racer races again, else the race
// is over: the mask the image keeps only); its line at the end.
void ObserverFill(Guest& g, Session& s, SpectatorShow& show, const spectator::Racers& racers, u32 machines, u32 count,
                  u32 step);
void ObserverGameHud(Guest& g, Session& s, u32 module, spectator::View view, bool followed_races);
void ObserverOff(Guest& g, Session& s, u32 module, bool racing);
void ObserverSummary(const Session& s);
// The results' cameras (follow.py, the results camera's site): a spectator's game keeps its seat; the image's pick
// logged once, with the local machine's body as the game left it. Every frame, playback or not.
void ResultsCamera(Guest& g, const Session& s, u32 module);
// timeline.cpp, timeline_hits.cpp: the racer timeline. A front step's state read for events (the step just run);
// the timeline to the host when it changed; its lines at the end; an event in step order; the step's rail hits and
// hits by the one-second rule (before: each racer's flags the step before; approached: +0x4C1).
void TimelineStep(Guest& g, Session& s, u32 step);
void TimelineSend(Session& s);
void TimelineSummary(Session& s);
void TimelineAdd(TimelineState& t, const TimelineEvent& e);
void SetupColours(Timeline& line);  // the seats' colours and names from the race's set-up (stand-ins where none)
void TimelineHits(Guest& g, TimelineState& t, u32 machines, u32 step, const std::array<u32, 30>& before,
                  const std::array<u8, 30>& approached);

// rejoin.cpp
std::array<u8, RECORD> PadFor(const Session& s, u32 t);
void AskForTheSeatAgain(Session& s);
void SayTheRaceIsOver(Guest& g, Session& s, u32 step);
void CatchingUpOrLive(Guest& g, Session& s, u32 step);
bool CatchingUp(const Session& s, u32 step);
std::string ReplayToWrite(Session& s, const std::string& path);
void GiveTheReplayItsPlace(Session& s);
void LeaveTheCrashedReplay(Session& s);
// The game stops by no stop of its own while the player's seat still races: the seat may come back (Rejoin).
bool LeftWhileRacing(const Session& s);
// session.cpp: the step every racer's results page opens is said (and its time kept); a racer's game leaves that page
// by itself RESULTS_LEAVE after it, as with the player's A.
void SayResultsLeavable(Guest& g, Session& s);
void LeaveTheResultsInTime(Guest& g, Session& s);
// rumble.cpp: whether the player's controllers may rumble (fzgx::RumbleAllowed): `s` the session (none: a plain start,
// or a race before its session), `step` the step about to run.
bool RumbleWanted(const Options& o, const Session* s, u32 step);
// practice_session.cpp: a practice's session (no relay; the pad gives each step's row); its hook before every step
// (a displayed frame's first, or a step run again); its end (the kept run's last steps to the replay).
bool StartPractice(Guest& g);
void PracticeHook(Guest& g, Session& s, bool extra);
void PracticeEnd(Session& s);
bool PracticePaused();  // a practice stands at a moment (Start: its bar is up)

}  // namespace fzgx::net
