# Pockle

A small e-paper companion for a kid's desk: the day's Bible verse and devotional, the latest sports scores, and the weather — on a 4-inch screen that holds its image without power, with a rocker switch to browse and a voice you can ask questions. It runs on the Waveshare ESP32-S3-ePaper-3.97 board (480 × 800 e-paper, microphone, speaker, battery) and lasts weeks on a charge.

<p align="center">
  <img src="assets/photos/launcher.jpg" width="31%" alt="The launcher: weather, verse of the day, and the NFL scoreboard">
  <img src="assets/photos/devotional.jpg" width="31%" alt="Today's devotional, with READ ALOUD, BIBLE and DONE buttons">
  <img src="assets/photos/matchup.jpg" width="31%" alt="A matchup page: logos, score, box score, team leaders and headline">
</p>

## What's on the device

**Launcher** — the first screen
- Weather, today's verse, and the latest scores across every league
- Three doors: the forecast, the Bible, and the scoreboard

**Bible**
- The whole Bible on the board, readable offline
- Four translations with a one-line description of each: Berean Standard, Free Bible Version, Bible in Basic English, King James
- Book and chapter picker, pages turn across chapters and books, reading position remembered
- Small, normal or large text

**Daily devotional**
- One screen a day for ages 7–13: the verse, a title, one truth, three bullets, something to do today, a one-line prayer
- Gospel-centered — points to what Jesus has done before what we do
- Read aloud by the speaker
- The verse of the day everywhere on the device is the devotional's verse
- Written as Markdown files in `devotionals/`, delivered over Wi-Fi, kept on the board for offline reading; a day with no file gets one written on the spot

**Sports scores**
- MLB, NFL, NBA and college football
- Every scoreboard reads as a timeline: LIVE, then UP NEXT (soonest first, with kickoff times), then RESULTS — so after the week's last game you see the coming slate without any extra clicks
- NFL and college football carry this week and next; MLB and NBA carry today and tomorrow; college football shows Top-25 games only, with ranks on the rows
- The launcher shows each league's NEXT game above its latest results
- Dense rows grouped by league and week, with team logos
- Matchup pages: box score, team leaders, headline
- Team pages: results, next game, division and conference standings
- Refreshes while awake and whenever it wakes up

**Weather**
- Current conditions on the launcher
- Seven-day forecast page for your ZIP code

**Voice** — hold the rocker and ask
- "Bears score", "NFL standings", "upcoming NFL games", "go to Psalm 23", "read today's devotional", "will it rain tomorrow", "who won the 1985 Super Bowl"
- "Upcoming NFL games" opens a schedule page: the coming week's games only, grouped by day
- Opens the right page or answers out loud, in a voice you choose
- Sticks to sports, the Bible and the weather; looks up current facts rather than guessing

**Settings**
- Wi-Fi setup from a phone
- Spoken replies on/off, speaking voice, dark mode, nightly sleep schedule
- Check for software updates

**Battery life**
- Sleeps when idle and overnight, showing the verse of the day
- Wakes on any button; one timed wake each morning for the day's devotional, verse, weather, scores and updates
- A few percent of battery a day

## Setting one up

1. Flash the firmware over USB (`tools/build.sh`, then `tools/flash.sh <port>`), then load the Bible and devotionals (`tools/upload_bible.sh <port>`). `tools/bootstrap.sh` installs the build tools the first time.
2. Add the voice key over USB (`tools/device_console.py key`); it stays on the device and is never in this repository.
3. Turn it on and join its Wi-Fi setup page from a phone: home Wi-Fi, timezone, and a ZIP code for the weather.

After that the device looks after itself: it checks GitHub for new firmware each morning and installs it on its own, and pulls new or edited devotionals the same way.

## Making changes

- **Devotionals:** add or edit `devotionals/MM-DD.md`, run `tools/build_devotionals.py` (it checks each one fits the screen, and can draft missing days), commit and push. Devices pick them up within hours.
- **Firmware:** `tools/release.sh 1.7.0 "notes"` runs the tests, builds, tags, and publishes a release that every device installs over Wi-Fi.
- **Checking a device:** `tools/device_console.py` talks to a board over USB — status, a screen capture, a forced devotional or weather fetch, a storage map.

`VALIDATION.md` keeps the running log of what was built, why, and how each piece was verified on the hardware; `PRD.md` holds the product intent.

## License

The code, tools and documentation are released under the [MIT License](LICENSE). The devotionals are [CC BY 4.0](devotionals/LICENSE). Third-party components keep their own terms, listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and below.

## Credits and licenses

- Bible text: the Berean Standard Bible (public domain, Bible Hub), the Free Bible Version (© 2018 Jonathan Gallagher, CC BY-SA 4.0), the Bible in Basic English and the King James Version (public domain), the latter three from [eBible.org](https://ebible.org).
- Scores from ESPN's public endpoints; weather from [Open-Meteo](https://open-meteo.com); voice, speech and devotional drafting through OpenRouter.
- Team logos are the property of their owners and are used for identification. Inter font under the SIL Open Font License (`assets/fonts/OFL.txt`).
- Built on the [Waveshare ESP32-S3-ePaper-3.97](https://docs.waveshare.com/ESP32-S3-ePaper-3.97) with its display driver (`vendor/`), [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) and [ArduinoJson](https://arduinojson.org/).
