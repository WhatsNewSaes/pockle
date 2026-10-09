# Retro Sports Companion

Status: Product requirements agreed in conversation; hardware and data-provider validation pending.

## Purpose

A portable, retro 8-bit sports dashboard for a child who follows multiple sports. Make it easy to browse scores, schedules, and favorite teams using physical controls, with a later voice assistant accessible by holding the rocker.

## Hardware

Target: Waveshare ESP32-S3-ePaper-3.97, 800 × 480 e-paper display, Wi-Fi, microphone, three-way rocker, BOOT, and PWR buttons. Spoken replies depend on an attached speaker. Validate controls, display orientation, refresh behavior, and power behavior on the actual board before finalizing implementation.

## Initial leagues

- MLB
- NFL
- NBA
- College football; exact competition coverage depends on the selected data provider and must be made explicit in the interface.

## Experience and navigation

Home presents the four leagues and Favorites. Selecting a league opens its games. Selecting a game opens a scorecard with team names, scores, game status, and period/inning where available. Support today's games, recent results, and upcoming games through a date selector. Clearly distinguish scheduled, live, final, postponed, and canceled games using available source data.

Followed teams come first: the MINE tab lists their games across every league, each team's next game leading. Teams are followed from the parent's phone page (planned) or by voice ("follow the Bears", "stop following the Bears"); the device carries no per-team favoriting controls. Followed teams persist through restarts.

Empty schedules show a friendly message and an option to browse another date. Missing data must never appear as a fabricated score or a zero score.

## Controls

| Input | Intended behavior |
| --- | --- |
| Rocker up/down | Move through lists or available detail pages |
| Rocker short press | Select/open |
| Rocker long hold | Activate the voice assistant; continue holding to speak, release to submit |
| BOOT short press | Back one level; at Home, remain at Home |
| PWR | Power/sleep; final behavior subject to hardware verification |

A long hold must not also trigger a short-press selection. The hold threshold requires usability testing. BOOT retains its hardware programming role during startup. Voice activation belongs to the rocker, not BOOT. During the first release, before voice is implemented, a long hold explains that voice is not yet available without changing the current selection.

## Visual direction

Retro handheld sports game styling: pixel typography, chunky selection arrows, simple sports sprites, framed scorecards, and strong black-and-white contrast. Large scores and clear team labels take priority over decoration. Use static pixel celebrations for completed games where appropriate. Avoid continuous animation and blinking elements. Show selection clearly without relying on color. Choose typography that remains readable at the physical display size.

## Score freshness and offline behavior

Always show the last successful data-update time for the displayed information, formatted in the configured local timezone. A fetch attempt or display redraw must not advance this timestamp.

When Wi-Fi is disconnected, show a persistent, conspicuous banner such as:

> OFFLINE — Saved scores · Updated Sep 29, 7:42 PM ET

Keep the most recently retrieved scores and schedules visible and navigable. Persist the last successful snapshots and their timestamps locally so they survive sleep or a restart. Each league/date snapshot retains its own timestamp; do not apply another league's successful update time to it.

A cached in-progress game must be identified as saved information rather than presented as currently live. Do not advance a game clock or infer score changes while offline. Dates must remain visible when cached data is from a previous day.

If the requested league/date has no saved data, show “No saved scores for this view. Connect to Wi-Fi to load them.” Do not substitute another date's games silently.

If Wi-Fi is connected but the sports service cannot be reached, show a distinct “Scores unavailable — showing saved data” banner and the original timestamp. Retain valid cached data when a request fails or returns malformed data.

Retry connectivity in the background with backoff. Clear the stale-data warning for a view only after fresh data for that view loads successfully. Avoid repeated screen refreshes just to retry requests.

## First release

- Wi-Fi setup with a clear connection status and a way to change networks.
- Four-league home screen, game lists, scorecards, and date navigation.
- Scores, supported game statuses, upcoming games, and favorite teams.
- Consistent physical navigation and retro graphics.
- Persistent cached scores, accurate update timestamps, and offline/service-error banners.
- Automatic data updates with a cadence chosen after checking provider limits, power use, and screen behavior.

## Voice release

Hold the rocker to speak requests such as “Show MLB games,” “What's the Yankees score?” or “When do the Chiefs play next?” Display a listening indication, then processing, then the requested league or game and an answer. Preserve the previous screen when canceled or unsuccessful.

Use an online speech/assistant service and retrieve scores from the selected sports data source. Answers about scores must be grounded in retrieved data, not generated from model memory. Clearly distinguish current results from saved results and state their update time when using saved information.

If a request is ambiguous, offer choices navigable with the rocker. Initially limit the assistant to supported sports, schedules, and navigation. A general-purpose assistant is outside the initial scope. Spoken responses are optional and require working speaker hardware.

When offline, holding the rocker explains that voice requires internet and returns to the cached sports interface. Record only during deliberate voice activation. Keep service credentials out of shared source files; decide how credentials and any backend are managed before implementation. Confirm child-use requirements and operating costs for the chosen services.

## Acceptance criteria

1. A user can open each supported league, browse its games, inspect a game, and return Home using only the physical controls.
2. Short-press selection and long-hold voice activation never both fire for one hold.
3. Favorites persist after a restart.
4. Disconnecting Wi-Fi leaves the latest saved scores available with a visible offline banner and their actual update date/time.
5. Restarting offline restores cached data and freshness information; an uncached view clearly says no saved data exists.
6. A failed sports-service request does not erase valid cached scores or update their successful-fetch timestamp.
7. Reconnecting and successfully fetching the displayed view refreshes its information and removes its stale-data warning.
8. Scheduled, final, postponed, canceled, and cached in-progress states remain distinguishable where supported by the provider.
9. Menus and scores are readable on the physical screen, with responsive navigation and an acceptable refresh/ghosting balance.
10. In the voice release, a rocker hold can open a requested supported league or game; ambiguous and failed requests have usable recovery paths.

## Decisions and validation remaining

- Sports provider: coverage, college football subdivisions, licensing, reliability, authentication, costs, and request limits.
- Wi-Fi onboarding method and whether score fetching needs a backend.
- Refresh cadence, partial/full refresh strategy, cache retention, and battery/sleep policy.
- Hardware verification: rocker hold detection, BOOT input, PWR behavior, microphone, and speaker.
- Timezone setup and trustworthy timekeeping after an offline restart.
- Voice provider, credential storage, latency, costs, and child-use requirements.

## References

- [Waveshare board documentation](https://docs.waveshare.com/ESP32-S3-ePaper-3.97)
- [Waveshare Arduino examples](https://docs.waveshare.com/ESP32-S3-ePaper-3.97/Arduino)

Hardware capabilities are documented by Waveshare. Product behavior above is the intended custom implementation, not a claim about the factory firmware.

## Display refinement — September 29, 2026

Use 480 × 800 portrait orientation, rotated to match the user’s upright holding position. Show monochrome team logos beside scores, enlarged on game details, and in favorites. Bundle logo assets locally for offline use; preserve the existing saved-score timestamp and offline banner. Use an abbreviation badge when a team logo is unavailable.

## Scoreboard refinement — September 29, 2026

Opening a league shows its recent games newest first regardless of today's schedule (a rolling window per league), with the single-day picker still available. Score pages are dense: one row per game reading away score, away logo, matchup and status, home logo, home score, ten games per page. Each row states its day. Physical navigation must update the screen on every press; full refreshes for ghost cleanup happen only while idle.

## Bible and verse of the day — September 30, 2026

The device carries the complete Berean Standard Bible (public domain) on its data partition and reads it offline. The first screen becomes a launcher: a weather strip (current temperature, high/low, rain chance, condition) across the top that opens a 7-day forecast page, the verse of the day beneath it and the latest scores beneath, with two doors, the Bible and the sports scores. The Bible home offers continue reading (resuming the saved position), the verse of the day, and a book/chapter picker; the sports door opens the scoreboard home with its league tabs. The reader uses Inter at a 20 px em (capitals about 14 px), turns pages with the rocker across chapter and book boundaries, and remembers where the reader stopped. Voice opens the Bible, a book, chapter or verse, the verse of the day, and reads a passage aloud when asked; its scope widens from sports only to sports and the Bible. Sleep screens show the verse of the day. The text is loaded once with `tools/upload_bible.sh`, independent of firmware flashes; no scripture is compiled into the firmware beyond the book table and the verse-of-the-day reference list.
