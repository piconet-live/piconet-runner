// The core's test program: runs the tests of test_*.cpp one after another against a host that is 24 MiB of plain
// memory (harness.h), and holds their time limit. Run by tests/run.sh.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "harness.h"
#include "files.h"
#include <atomic>  // this and the next four: the tests' time limit
#include <chrono>
#include <cstdlib>
#include <thread>
#include <unistd.h>

// test_hfr.cpp
void TestTheRefreshesBetweenTwoSteps();
void TestTheBlendOfAPoseAndAView();
void TestTheCorrections();
void TestAPassOfPicturesBetween();
void TestTheGuessesForward();
// test_guest.cpp
void TestGuestMemory();
void TestHelpers();
// test_rollback.cpp
void TestUndoLog();
void TestThePageGuard();
void TestThePageGuardBesideTheEmulatorsHandler();
void TestThePageGuardWhenItsMemoryIsGone();
void TestAnotherThreadsWriteUnderRollback();
void TestTheLockedCacheInTheUndoLog();
void TestTheRollbackInterface();
void TestChecksum();
void TestTheSpareListsAreBounded();
void TestARollbackLeavesTheWindow();
// test_rollback_state.cpp
void TestRollbacksStateList();
// test_files.cpp
void TestRaceFromAMatchFile();
void TestAReplaysLineup();
void TestAReplayThatIsNotPlayed();
void TestAReplayOfMoreThanSixHours();
void TestASetUpTheGameDoesNotHave();
void TestASeatsCustomMachine();
void TestACustomMachineOnAStatPack();
void TestACustomMachinesOwnPartWordsOnAStatPack();
// test_relay.cpp
void TestARelayWhoseClockIsNoClock();
void TestAGameClosedMidRaceEndsAsLeftRacing();
void TestTheExitWhenTheGameStopsWhileItsSeatRaces();
void TestTheResultsAreLeftThirtySecondsAfterEveryRacersPage();
void TestTheFirstResultsPageIsPassedAfterFifteenSeconds();
void TestRumble();
void TestTheStatusBoardOfAPlainStart();
void TestABoardThatIsNotThisVersionsIsLeft();
void TestTheRaceOnTheBoard();
void TestTheCameraViewOnTheBoard();
void TestTheSessionsPhasesOnTheBoard();
void TestARelayThatNamesSeatsAndFramesOfNoRace();
void TestAPacketWithoutTheSeatsKeyIsNotTheRelays();
void TestAJoinedWhoseSeatIsNoPlayerSeatIsRefused();
void TestAGameStartedIntoARaceThatIsOverStops();
void TestAGameStartedAgainTakesTheReplaysPlace();
void TestASpectatorSendsNoInputAndWatchesBehindLive();
void TestTheRacePhaseBackAfterTheRaceSettled();
// test_race_end.cpp
void TestTheRaceEndsOnceOnOfficialInputs();
void TestTheFinishLimitInTheRaceBlock();
// test_pace.cpp
void TestTheGameKeepsToItsSchedule();
void TestAGameAheadOfItsScheduleFallsBack();
void TestASlowHostSendsItsInputsInTime();
void TestTheScheduleAtALowerDelay();
void TestARaceLeavesItsSummaryLine();
void TestTheSummaryCountsBursts();
void TestTheSummarysEndsAndItsFile();
void TestTheHostsPicturesReachTheSummaryLine();
void TestASummaryThatFailsCostsNoReplay();
// test_spectate.cpp
void TestAWatchingGameRunsBehindOnOfficialFrames();
void TestAGameThatStandsStillIsReported();
void TestAStopWhileTheGameStands();
void TestTheSummaryLineSaysAFreeze();
// test_panel.cpp
void TestWhatThePanelShows();
void TestThePanelHeldToTheRace();
void TestThePanelHeldUntilLive();
void TestThePanelsLines();
void TestThePanelFromAStartToTheCourse();
void TestThePanelAtTheStartGate();
// test_panel_art.cpp
void TestAPackedFileIsUnpacked();
void TestATexturesPixels();
void TestAGamePictureScaledForTheScreen();
void TestTheLoadsProgress();
void TestTheSettingThatMakesAGameStand();
void TestARaceThatEndsWhileTheGameStands();
void TestAWaitForTheRelayAndTheCountdownsThreshold();
void TestAFailedSessionEndsItsUndoLog();
void TestALineWithoutThePlayersFolders();
void TestWhichOfTheEmulatorsLinesAreWritten();
void TestTheLogFileAStartWrites();
// test_recording.cpp
void TestAPlaybackWithForcedRollbacks();
void TestARecordingRoundTrip();
// test_scrubber.cpp, test_scrubber_seek.cpp
void TestTheReplayScrubber();
void TestTheScrubbersSeeks();
// test_practice.cpp
void TestPracticeWithRewind();
// test_spectator.cpp
void TestTheSpectatorsPad();
void TestTheCourseMapReadsTheRoad();
void TestTheCourseMapsCamera();
void TestTheCourseMapsViewTurns();
void TestTheCourseMapsRoadForTheHost();
void TestTheCourseMapsDots();
void TestWhenTheCourseMapShows();
void TestTheCourseMapsOffsetsAreTheLaunchers();
// test_minimap_start.cpp
void TestTheCourseMapsStartLine();
void TestTheCourseMapsStartMark();
// test_minimap_view.cpp
void TestTheCourseMapsViewTilts();
void TestInputsStickAtRestStandsInItsGate();
void TestInputsStickUpMovesUpAndLeansAway();
void TestInputsProjectiveMapsCornersAndBack();
void TestInputsTriggerIsFullAtItsRangeOrAPress();
void TestTheCourseMapStaysInItsSquare();
void TestAKnockedOutRacerNeverScrubs();
void TestTheLeaderAndTheClosestBattle();
void TestTheRacerBeforeAndAfter();
void TestTheFollowModes();
void TestTheCameraKinds();
void TestTheOverlaysFollowButtons();
void TestACoursesRule();
void TestTheResultsCamerasPick();
// test_observer.cpp
void TestTheFrontsClock();
void TestTheGapsToTheLeader();
void TestTheTowersHeadAndTexts();
// test_native_form.cpp
void TestTheNativeForm();
void TestAStoryStageIsNamedInTheGame();
// test_strip.cpp
void TestTheStripsLayout();
void TestWhatAPointOfTheWindowIs();
void TestTheFrameRateInATitle();
// test_launch.cpp
void TestASavesControllerOptions();
void TestASaveThatIsRefused();
void TestARaceTakesThePlayersControllerOptions();
void TestARaceStartsInThePlayersView();
void TestThePlayersOptionsUnderRollback();
void TestSavesDamagedAtRandom();
void TestALaunchFile();
void TestALaunchFileThatIsRefused();
void TestThePartialRollbackKeys();
void TestALaunchFilesTaskbarButton();
void TestALaunchFileOfTheEmulatorsSide();
void TestTheWindowsControlsAndTheUsersSettings();
void TestARunnerOptionALaunchFileMustAllow();
void TestTheNamedLaunchFiles();
// test_present.cpp
void TestALateFramesDrawingIsSkipped();
void TestALaunchFileOfTheLaunchers();  // test_launch_vectors.cpp
void TestTheLaunchFilesPlayersKeys();  // test_launch_latency.cpp: presentation, delay, lanes, true widescreen
void TestTheWindowsKeptPlace();
void TestThePicturesWaitForAConsolesMoment();
void TestWhatOfTheWindowsPlaceIsKeptAndWhen();
void TestTheAskingForTheBackendsOwnFullScreen();
void TestWhatAWayOfShowingAPictureNeedsOfTheEmulator();
// test_stats.cpp
void TestAStatPackOfAMatch();
void TestTheStatRangesAreTheHandbooks();
void TestTheStatCasesOfBothPrograms();
void TestAStatPackInARecording();
void TestTheMineRuleInTheRaceBlock();
void TestTheMineRuleInARecording();
void TestTheStartGrid();
// test_out_test.cpp
void TestTheOutTestsCases();
void TestTheOutTestReadsTheGame();
void TestTheModeVectorsRead();
void TestTheBoardsLinesAndValues();
void TestPracticeIsAMode();
void TestAnUnknownModeFieldPassesThrough();
void TestAGonePlayerRetires();
void TestTheModeTagWords();
void TestTheCutRaceWords();
void TestTheStatWords();
void TestAVecIsBuilt();
void TestAFaultShowsOnTheResults();
void TestTheHeldWords();
void TestThePedalLatch();
void TestTheStatsPage();
void TestTheEnergyWords();
void TestTheCombatWords();
void TestTheOnOffWordsByParameter();
void TestASquareRootsCostInBothLoops();
void TestSoloModesAndTheRelay();
void TestDestroyedAndOutTellAnEndApart();
void TestOverdriveRunsInTheBlock();
void TestEveryWordRuns();
void TestTheGamesSquareRootIsTheCompilers();
void TestTheGeneratorIsTheReferences();
void TestWaitsRunInTheBlock();
void TestTheLanguageVectorRuns();
void TestGameStartComesFirst();
void TestTheRaceWordsRun();
void TestStartTogetherKeepsEveryPairApart();
void TestTheKnockoutEndsWithItsPlaces();
void TestARaceThatWaitsRanksTheOut();
void TestTheLivesLibraryRuns();
void TestCpuModeEighteen();
void TestTheDisplayWordsRun();
void TestTheGhostWords();
void TestThePreparedInterpreterIsThePlainOne();
void TestTheLobbysRestoreStaysOut();
// test_stats_race.cpp
void TestAStatPackIsLaidOverTheCarData();
void TestAStatPackUnderRollback();
void TestAStatPackAtTheGo();
void TestAStatPacksConstants();
// test_options_screen.cpp
void TestTheControllerSetupStart();
void TestTheControllerSetupKey();
// test_hits.cpp
void TestHitsAFrameShowedAreNotShownAgain();
void TestARollbackShowsTheHitItRevealed();
void TestAHitThePredictionShowedIsNotShownAgain();
void TestAnOldHitIsNotShown();
void TestARollbackShowsTheLungeItRevealed();
void TestAHitInANewStepIsTheGamesOwn();
void TestTheHitRumbleIsAskedOnce();

int s_failed = 0;
int s_checks = 0;

namespace
{
// A time limit per test (a test that hung kept a commit's check waiting for ever, naming no test). main() calls the
// Test... functions in turn: a test's time runs from its first CHECK to the next test's (the first's from the start;
// a test whose first check is in one of its steps begins with BEGIN_TEST). One still running TEST_TIME_LIMIT_SECONDS
// later is printed with its latest check's line and the program ends with status 3 (nothing else ends a hung thread).
// Many times the slowest test on a busy PC; FZGX_TEST_TIME_LIMIT_SECONDS sets another.
constexpr int TEST_TIME_LIMIT_SECONDS = 600;

long long NowMs()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::atomic<const char*> s_test{"the program before its first check"};  // the test running
std::atomic<long long> s_test_began{NowMs()};
std::atomic<const char*> s_check_file{__FILE__};  // the latest CHECK
std::atomic<int> s_check_line{0};
std::atomic<long long> s_check_time{NowMs()};
}  // namespace

void AtCheck(const char* function, const char* file, int line)
{
  const long long now = NowMs();
  // CHECKs in a test's helpers and lambdas (other names) belong to the test that called them
  if (function != s_test.load() && std::strncmp(function, "Test", 4) == 0)
  {
    s_test = function;
    s_test_began = now;
  }
  s_check_file = file;
  s_check_line = line;
  s_check_time = now;
}

namespace
{
void WatchTheTimeLimit()
{
  int limit = TEST_TIME_LIMIT_SECONDS;
  if (const char* text = std::getenv("FZGX_TEST_TIME_LIMIT_SECONDS"); text && std::atoi(text) > 0)
    limit = std::atoi(text);
  for (;;)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const long long now = NowMs();
    if (now - s_test_began > limit * 1000LL)
    {
      std::printf("TIME LIMIT: %s ran longer than a test's time limit (%d s); the latest check was at %s:%d, %lld s "
                  "ago. The test program ends here.\n",
                  s_test.load(), limit, s_check_file.load(), s_check_line.load(), (now - s_check_time) / 1000);
      std::fflush(stdout);
      _exit(3);
    }
  }
}

// started before main(), on a thread of its own
const bool s_watching = (std::thread(WatchTheTimeLimit).detach(), true);
}  // namespace

// What ReadSavedOptions says of a folder: the file it took and the options a race would write (port 1's dead zones
// and its standard controller's nine bindings), or why it took none.
static int SaveOptionsOf(const char* folder)
{
  const boot::SavedOptions saved = boot::ReadSavedOptions(folder);
  if (!saved.options)
  {
    std::printf("save options: refused: %s\n", saved.refused.c_str());
    return 1;
  }
  std::string bindings;
  for (int i = 0; i < 0x1C; ++i)
    bindings += std::format("{:02x}", saved.options->bindings[i]);
  std::printf("save options: taken from %s: port 1 dead zones %d %d, bindings %s; %zu more file(s) of the save left out\n",
              saved.path.c_str(), saved.options->dead_zones[0], saved.options->dead_zones[1], bindings.c_str(), saved.left_out.size());
  return 0;
}

// The undo log, and its guard by page protection (test_page_guard.cpp).
void TestTheUndoLogAndItsGuard()
{
  TestUndoLog();
  TestThePageGuard();
  TestThePageGuardBesideTheEmulatorsHandler();
  TestThePageGuardWhenItsMemoryIsGone();
}

// The panel before a race (test_panel.cpp): what it shows, its lines, font and picture, the starts, the start gate.
void TestThePanelBeforeARace()
{
  TestWhatThePanelShows();
  TestThePanelHeldToTheRace();
  TestThePanelHeldUntilLive();
  TestThePanelsLines();
  TestThePanelFromAStartToTheCourse();
  TestThePanelAtTheStartGate();
}

// The hits and lunges a rollback reveals (test_hits.cpp): each shown once, never what a frame already showed.
void TestTheHitsARollbackReveals()
{
  TestHitsAFrameShowedAreNotShownAgain();
  TestARollbackShowsTheHitItRevealed();
  TestAHitThePredictionShowedIsNotShownAgain();
  TestAnOldHitIsNotShown();
  TestARollbackShowsTheLungeItRevealed();
  TestAHitInANewStepIsTheGamesOwn();
  TestTheHitRumbleIsAskedOnce();
}

// A race against a relay that is the test itself (test_relay.cpp): relays not ours, a game closed mid-race (and the
// exit decision, test_left_racing.cpp), a game started again, the race phase back after the race settled.
void TestARaceAgainstARelay()
{
  TestARelayWhoseClockIsNoClock();
  TestAGameClosedMidRaceEndsAsLeftRacing();
  TestTheExitWhenTheGameStopsWhileItsSeatRaces();
  TestTheResultsAreLeftThirtySecondsAfterEveryRacersPage();
  TestTheFirstResultsPageIsPassedAfterFifteenSeconds();
  TestRumble();
  TestTheStatusBoardOfAPlainStart();
  TestABoardThatIsNotThisVersionsIsLeft();
  TestTheRaceOnTheBoard();
  TestTheCameraViewOnTheBoard();
  TestTheSessionsPhasesOnTheBoard();
  TestARelayThatNamesSeatsAndFramesOfNoRace();
  TestAPacketWithoutTheSeatsKeyIsNotTheRelays();
  TestAJoinedWhoseSeatIsNoPlayerSeatIsRefused();
  TestAGameStartedIntoARaceThatIsOverStops();
  TestAGameStartedAgainTakesTheReplaysPlace();
  TestASpectatorSendsNoInputAndWatchesBehindLive();
  TestTheRacePhaseBackAfterTheRaceSettled();
}

// The host's finish limit (test_race_end.cpp) and mine rule (test_mines.cpp): their words in the race block, and a
// recording of a race with the mine rule.
void TestTheHostsRaceRules()
{
  TestTheFinishLimitInTheRaceBlock();
  TestTheMineRuleInTheRaceBlock();
  TestTheMineRuleInARecording();
  TestTheStartGrid();
}

// How a picture is shown: at a console's moment; none drawn in a late frame (test_present.cpp).
void TestThePictures()
{
  TestThePicturesWaitForAConsolesMoment();
  TestALateFramesDrawingIsSkipped();
}

void TestTheSpectatorControls()
{
  TestTheSpectatorsPad();
  TestTheCourseMapReadsTheRoad();
  TestTheCourseMapsCamera();
  TestTheCourseMapsViewTurns();
  TestTheCourseMapsRoadForTheHost();
  TestTheCourseMapsDots();
  TestWhenTheCourseMapShows();
  TestTheCourseMapsOffsetsAreTheLaunchers();
  TestTheCourseMapsStartLine();
  TestTheCourseMapsStartMark();
  TestTheCourseMapsViewTilts();
  TestInputsStickAtRestStandsInItsGate();
  TestInputsStickUpMovesUpAndLeansAway();
  TestInputsProjectiveMapsCornersAndBack();
  TestInputsTriggerIsFullAtItsRangeOrAPress();
  TestTheCourseMapStaysInItsSquare();
  TestAKnockedOutRacerNeverScrubs();
  TestTheLeaderAndTheClosestBattle();
  TestTheRacerBeforeAndAfter();
  TestTheFollowModes();
  TestTheCameraKinds();
  TestTheOverlaysFollowButtons();
  TestACoursesRule();
  TestTheResultsCamerasPick();
  TestTheFrontsClock();
  TestTheGapsToTheLeader();
  TestTheTowersHeadAndTexts();
}

// A race's stat pack: the reader, the ranges, a recording, the host's rules beside it, the pack in memory, constants.
void TestStatPacks()
{
  TestAStatPackOfAMatch();
  TestTheStatRangesAreTheHandbooks();
  TestTheStatCasesOfBothPrograms();
  TestAStatPackInARecording();
  TestTheHostsRaceRules();
  TestAStatPackIsLaidOverTheCarData();
  TestAStatPackUnderRollback();
  TestAStatPackAtTheGo();
  TestAStatPacksConstants();
}

// test_hfr.cpp's
void TestTheHighRefreshRate()
{
  TestTheRefreshesBetweenTwoSteps();
  TestTheBlendOfAPoseAndAView();
  TestTheCorrections();
  TestAPassOfPicturesBetween();
  TestTheGuessesForward();
}

// When a picture is shown (test_place.cpp's, test_screen.cpp's) and the pictures between two steps (test_hfr.cpp's)
void TestThePresentation()
{
  TestThePicturesWaitForAConsolesMoment();
  TestWhatAWayOfShowingAPictureNeedsOfTheEmulator();
  TestTheHighRefreshRate();
}

int main(int argc, char** argv)
{
  // --hang: the time limit's own test (run.sh runs it with a limit of one second). Nothing is checked and nothing
  // ends: only the limit can end this run, with status 3 and its line.
  if (argc > 1 && std::strcmp(argv[1], "--hang") == 0)
    for (;;)
      std::this_thread::sleep_for(std::chrono::seconds(1));
  // --save-options FOLDER: no test. The game's own reader of a player's saved game (files_save.cpp) on a folder of
  // save files, for a script that has to say whether a save a game wrote is one a race would take
  // (port/gates/runs/controller-setup-run.sh). One line, and status 0 when the options were taken, 1 when refused.
  if (argc > 2 && std::strcmp(argv[1], "--save-options") == 0)
    return SaveOptionsOf(argv[2]);
  TestGuestMemory();
  TestHelpers();
  TestTheUndoLogAndItsGuard();
  TestAnotherThreadsWriteUnderRollback();
  TestTheLockedCacheInTheUndoLog();
  TestRollbacksStateList();
  TestTheRollbackInterface();
  TestARollbackLeavesTheWindow();   // before any Init: the core's log then has no host to go to
  TestChecksum();
  TestTheSpareListsAreBounded();
  TestRaceFromAMatchFile();
  TestAReplaysLineup();
  TestAReplayThatIsNotPlayed();
  TestAReplayOfMoreThanSixHours();
  TestASetUpTheGameDoesNotHave();
  TestASeatsCustomMachine();
  TestACustomMachineOnAStatPack();
  TestACustomMachinesOwnPartWordsOnAStatPack();
  TestARaceAgainstARelay();
  TestTheRaceEndsOnceOnOfficialInputs();
  TestTheGameKeepsToItsSchedule();
  TestAGameAheadOfItsScheduleFallsBack();
  TestASlowHostSendsItsInputsInTime();
  TestTheScheduleAtALowerDelay();
  TestARaceLeavesItsSummaryLine();
  TestTheSummaryCountsBursts();
  TestTheSummarysEndsAndItsFile();
  TestTheHostsPicturesReachTheSummaryLine();
  TestASummaryThatFailsCostsNoReplay();
  TestAWatchingGameRunsBehindOnOfficialFrames();
  TestAGameThatStandsStillIsReported();
  TestAStopWhileTheGameStands();
  TestTheSummaryLineSaysAFreeze();
  TestThePanelBeforeARace();
  TestAPackedFileIsUnpacked();
  TestATexturesPixels();
  TestAGamePictureScaledForTheScreen();
  TestTheLoadsProgress();
  TestTheSettingThatMakesAGameStand();
  TestARaceThatEndsWhileTheGameStands();
  TestAWaitForTheRelayAndTheCountdownsThreshold();
  TestAFailedSessionEndsItsUndoLog();
  TestALineWithoutThePlayersFolders();
  TestWhichOfTheEmulatorsLinesAreWritten();
  TestTheLogFileAStartWrites();
  TestAPlaybackWithForcedRollbacks();
  TestARecordingRoundTrip();
  TestTheReplayScrubber();
  TestTheScrubbersSeeks();
  TestPracticeWithRewind();
  TestTheSpectatorControls();
  TestTheNativeForm();
  TestAStoryStageIsNamedInTheGame();
  TestASavesControllerOptions();
  TestASaveThatIsRefused();
  TestARaceTakesThePlayersControllerOptions();
  TestARaceStartsInThePlayersView();
  TestThePlayersOptionsUnderRollback();
  TestSavesDamagedAtRandom();
  TestALaunchFile();
  TestALaunchFileThatIsRefused();
  TestThePartialRollbackKeys();
  TestALaunchFilesTaskbarButton();
  TestALaunchFileOfTheEmulatorsSide();
  TestTheWindowsControlsAndTheUsersSettings();
  TestARunnerOptionALaunchFileMustAllow();
  TestTheNamedLaunchFiles();
  TestALaunchFileOfTheLaunchers();
  TestTheLaunchFilesPlayersKeys();
  TestTheWindowsKeptPlace();
  TestThePictures();
  TestWhatOfTheWindowsPlaceIsKeptAndWhen();
  TestTheAskingForTheBackendsOwnFullScreen();
  TestThePresentation();
  TestTheStripsLayout();
  TestWhatAPointOfTheWindowIs();
  TestTheFrameRateInATitle();
  TestTheControllerSetupStart();
  TestTheControllerSetupKey();
  TestTheHitsARollbackReveals();
  TestStatPacks();
  TestTheOutTestsCases();
  TestTheOutTestReadsTheGame();
  TestTheModeVectorsRead();
  TestTheBoardsLinesAndValues();
  TestPracticeIsAMode();
  TestAnUnknownModeFieldPassesThrough();
  TestAGonePlayerRetires();
  TestTheModeTagWords();
  TestTheCutRaceWords();
  TestTheStatWords();
  TestAVecIsBuilt();
  TestAFaultShowsOnTheResults();
  TestTheHeldWords();
  TestThePedalLatch();
  TestTheStatsPage();
  TestTheEnergyWords();
  TestTheCombatWords();
  TestTheOnOffWordsByParameter();
  TestASquareRootsCostInBothLoops();
  TestSoloModesAndTheRelay();
  TestDestroyedAndOutTellAnEndApart();
  TestOverdriveRunsInTheBlock();
  TestEveryWordRuns();
  TestTheGamesSquareRootIsTheCompilers();
  TestTheGeneratorIsTheReferences();
  TestWaitsRunInTheBlock();
  TestTheLanguageVectorRuns();
  TestGameStartComesFirst();
  TestTheRaceWordsRun();
  TestStartTogetherKeepsEveryPairApart();
  TestTheKnockoutEndsWithItsPlaces();
  TestARaceThatWaitsRanksTheOut();
  TestTheLobbysRestoreStaysOut();
  TestTheLivesLibraryRuns();
  TestCpuModeEighteen();
  TestTheDisplayWordsRun();
  TestTheGhostWords();
  TestThePreparedInterpreterIsThePlainOne();
  std::printf("%d checks, %d failed\n", s_checks, s_failed);
  return s_failed == 0 ? 0 : 1;
}
