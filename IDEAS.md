# Pocket ideas

A running log of pockets (the things a Pockle can hold: sports, the Bible, the devotional, weather, and whatever comes next) and the plumbing around them. Add to it freely; nothing here is committed to until it reaches `PRD.md`.

Vocabulary: a **pocket** is one feature a parent can switch on or off. We avoid "app".

Status words: **on the board** (shipped), **next** (agreed, not built), **idea** (open), **parked** (doesn't fit the hardware or the audience yet).

## Decisions so far (October 5, 2026)

- **Parents choose pockets, children adjust them.** Which pockets a board includes, their order, and set-once household settings (timezone, location, sleep schedule, Bible version, spoken replies master switch) are configured from a phone. In-the-moment preferences (text size, league, dark mode, speaking voice) stay on the device's rocker menus.
- **Configure from a page the board serves**, reachable as `pockle.local` on home Wi-Fi, with a QR code to it on the Settings screen. One profile document (JSON on the data partition) holds every setting and is applied at boot. A hosted service that delivers the same document can come later, only if families need remote or multi-board setup.
- **Setup by name.** Done October 9, 2026 with pockle.kids: a public A record `setup.pockle.kids → 192.168.4.1` (Vercel DNS), the second QR code, the "or visit" line and the captive redirect all use `http://setup.pockle.kids/`, the IP stays in small print, and the hostname stays plain http (pockle.kids sends HSTS without includeSubDomains, and .kids is not preloaded).

## Following teams (decided October 9, 2026)

- **Favorites are chosen on the phone page, not on the device.** Picking from a hundred-odd teams wants a keyboard and a scrolling list with logos, and the choice is made once a season: a household setting in the sense above. So the parent page gets a team picker grouped by league, tap to follow, and the board carries no per-team favoriting controls. Until that page exists, voice is the only way in.
- **Voice is the one on-device shortcut.** "Follow the Bears" and "stop following the Bears" (`follow_team` / `unfollow_team`). The reply confirms by name. A team can only be resolved through the saved games, so on a day with no saved game for that team the reply says to ask again on a game day. (On the board.)
- **The first tab is MINE, not ALL.** Two rows per followed team, grouped by league: its next game above its last result (a live game takes the result's place). With nobody followed yet, the same two rows per league, under one line saying how to follow a team, so a new board is never emptier than before. The old rule, every played game shared out evenly between leagues, gave the NFL four results and college one; the user said what matters is the last score and the next game, and nothing else. The league tabs still browse everything. (On the board.)
- **Gone:** the favorites star on game pages and the roster page under Settings, which had already lost their way in.

## On the board today

- **Launcher.** Weather, today's verse, the latest scores across every league; three doors to the forecast, the Bible and the scoreboard.
- **Bible.** The whole Bible offline in three translations (Berean Standard, Free Bible Version, Bible in Basic English), book and chapter picker, position remembered, three text sizes, read aloud verse by verse with pause and resume.
- **Daily devotional.** A page a day for ages 7 to 13 with a picture, the verse, a truth, three bullets, something to do, a prayer; read aloud; written in Markdown in the repo and synced over Wi-Fi; a missing day is written on the spot.
- **Bible characters.** Cards from Adam and Eve to Priscilla and Aquila; "tell me about Esther" opens hers.
- **Sports.** MLB, NFL, NBA and college football as a timeline (live, up next, results), matchup pages with box scores and leaders, team pages with standings; a MINE tab for the teams followed by voice.
- **Weather.** Current conditions on the launcher, a seven-day forecast page.
- **Voice.** Hold the rocker and ask; opens the right page or answers out loud; sticks to sports, the Bible and the weather.
- **Settings and care.** Wi-Fi setup by QR code from a phone, spoken replies and voice, dark mode, nightly sleep, updates that install themselves each morning; weeks on a charge.

## Checklist engine (next)

A titled checklist with a reset rule (once, daily, weekly) and a reward (completion screen with sparkles, a tally, a streak). Rocker moves and checks; progress lives in preferences; the whole pool is on the board, so it works offline. One engine powers all of the pockets below, so build it once with quests as the first content.

- **What the child sees.** A card on the launcher with the title and progress ("2 of 4"). Opening it shows the brief at the top and the items below, one highlighted. A press flips a box with a partial refresh. Finishing fills the screen with QUEST COMPLETE and the sparkles, bumps a tally, and extends a streak. A log page lists what has been finished.
- **What the parent sets.** Which lists are on, their reset rule, whether completions are reported, and the contents of parent-authored lists.
- **Under the hood.** A list is a small file: title, brief, items, reset rule, optional theme tag. Progress is a bitmask per list in preferences plus a short event log (list, item, time) for the parent page. A day of firmware on the devotional patterns.
- **Open questions.** Age bands (7 to 9 and 10 to 13 want different quests). Whether a streak should survive a sick day. Whether finished quests should earn something visible, like badges on the sleep screen.

### Quests (next, first content for the engine)

- Missions for outside and around the house. "Creation walk: find something God made that is red, something rough, something that makes a sound, something smaller than your thumb."
- Packs: nature, kindness ("do one thing for a sibling without being asked"), home helper, Bible explorer (find the verse that mentions a sparrow), Sunday (one question to ask after church), family (needs a parent to join in).
- The day's quest can share the devotional's theme so morning reading and afternoon mission connect. Weather picks an indoor quest on a rainy day. Seasons rotate packs.
- Online twist: an item can ask the child to hold the rocker and say what they found; the model judges "I found a red leaf" against "something red" and checks it for them.
- Content: aim for about ninety quests to start, drafted with the same tools that draft devotionals, every one checked to fit the screen.

### Chores and routines (next, parent-authored)

- "Morning: bed, teeth, Bible, breakfast." "After school: backpack, homework, feed the dog." Reset daily at the morning wake; weekly lists reset on a chosen day.
- The first thing the parent page manages. A parent types the items; the board shows them the next morning.
- Reporting: the nightly digest says "Morning routine 3 of 4, teeth skipped"; instant notifications are the opt-in.
- Idea on top: a star tally the parent can see, so rewards happen in the house rather than on the board.

### Reading plans (idea)

- The Gospel of John in 30 days, Psalms for the summer, Luke 1 and 2 through Advent, Proverbs a chapter a day.
- Each day's item opens the reader at that chapter, with read-aloud available; checking it off marks the day. A progress bar on the card.
- Parent picks the plan; the child can't skip ahead but can catch up.

### Memory verses (idea)

- The verse of the week, from a default list or set by the parent. Day one shows it whole, day three shows first letters, day five shows blanks.
- "Say it" holds the rocker; when online the model checks the recitation word by word and the box checks itself. Offline, the child checks it on their honor.
- A vault of verses known, and a streak of weeks.

### Prayer list (idea)

- Names and needs. "Add Grandma to my prayers" by voice; the parent can add from the page.
- "Prayed for today" check-offs, and a page of answered prayers that stays.

## Reporting back to parents (next, with the engine)

- **History on the parent page.** The board logs each check-off (list, item, time, nothing else). The page shows the week: quests finished, chores done today with what was missed, streaks. Stays in the house.
- **Notification on completion.** The board posts a one-line message ("Creation Walk complete, 4 of 4") to a private push topic the parent set on the page, or sends it through a transactional mail API. Works from anywhere, no server of ours.
- **Nightly digest.** Yesterday's summary posted at the 6:30 wake, as the default, with instant notifications as the opt-in.
- The destination is set only from the parent page; the child's side never shows it.

## Other pockets

### Bible trivia (idea)

- A question bank in the repo, drafted by the model and verified against the text, tagged by age band and book. Five questions a day; the rocker picks A, B or C; a score and a weekly best.
- "Quiz me" by voice starts a round. Wrong answers show the verse to read.

### Score predictions (idea)

- Before a favorite team's game the child picks the winner; the next morning the board scores the pick. A season tally on the team page. Fun, and it uses data already on the board.

### Countdowns (idea)

- Parent-set events: birthdays, Christmas, camp, a visit. The launcher shows the nearest; a big-number page counts the days; the sleep screen can say "3 days to Christmas."
- Automatic ones need no setup: the next game for a favorite team, Christmas, Easter.
- On the day itself the sleep screen celebrates.

### Quiet-time timer (idea)

- Presets of 10, 20 and 30 minutes, big digits updated by partial refresh each minute, a gentle chime from the speaker at the end.
- Ties to reading plans: a finished reading timer can check the day off. Also a family "screen-free" timer.

### Animals and places of the Bible (idea)

- Cards like the character cards: the lion, the sparrow, the fig tree; Jerusalem, the Jordan, Nazareth. Packs; "tell me about the lion" by voice.

### Thankfulness log (idea)

- When the devotional is marked done in the evening: "What is one thing you're thankful for today?" Said by voice, kept as a line of text with the date.
- The parent page shows the week's lines. Nothing is spoken back or shown on the child's side beyond today's.

### Hymn of the week (idea)

- Public-domain hymns only. Lyrics on the page, read aloud, a line about who wrote it and why.

### Family verse (idea)

- A verse the parent pins; it sits on the launcher under the verse of the day and on the sleep screen on Sundays.

## Surprising uses of e-ink (ideas)

E-ink's limits are the features: the image stays with zero power, it reads in full sun, it doesn't glow, the refresh is a slow flash, and the board sleeps for weeks and wakes on a timer.

Because the image stays with no power:
- **The night note.** A parent types a line on the phone at bedtime; the morning wake draws it on the desk. No battery, no notification, no unlock.
- **Never black.** The last thing a dying battery draws is the verse of the day and "charge me." A dead Pockle still shows scripture.
- **A mural that grows all year.** Every finished quest adds a tile to a picture (a stained-glass window, a map of Israel, the ark filling with animals), drawn once a day and simply kept. Carry it to a grandparent and it's still there.
- **Fridge mode.** A magnet on the back; the morning routine lives on the fridge all day, drawing nothing.

Because it reads in sunlight:
- **Trail guide quests.** Scavenger hunts outside at noon, where a phone is a mirror.
- **A sound hunt.** "Find something that sings; hold the rocker while it does." The model names the bird when the board is back online. A nature journal with no camera.

Because the refresh is a slow flash:
- **The reveal.** Trivia and memory verses use the flash as drama: the question sits still, then the page blinks to the answer, like turning a card over.
- **Time capsule.** "Say something to yourself a year from now." Drawn on that morning next year.

Because it sleeps for weeks and wakes on a timer:
- **Advent and Lent calendars.** One door per morning wake: a picture and a verse, for forty days on a shelf.
- **A sundial and moon.** Sunrise, sunset and tonight's moon phase from the location it already has, redrawn hourly.
- **The Daily Pockle.** A morning front page in newspaper layout: weather, the verse, last night's scores, today's quest. One-bit type on a paper-like panel is the one place this isn't a gimmick.

Because it doesn't glow:
- **The bedtime book.** The reader with a nightlight is the only bedroom screen that doesn't fight sleep. A "lights out" mode: one verse, large type.

Because it's one bit:
- **Woodcut collectibles.** Cards, seals and badges in the dithered style the scenes already use.
- **Hidden pictures.** A dither pattern that resolves into a lamb at arm's length.

Top three: the night note, the mural that grows all year, the Daily Pockle. All small to build.

## Parked

- **Photo or location proof for quests.** No camera, no GPS. Honor system and the voice judge instead.
- **Audio stories or music.** Ten minutes of speech is about 30 MB as the board plays it today; the data partition holds 5 MB free. Would need a compressed format and a storage rethink.
- **Messages between boards.** Needs a cloud relay; revisit with the cloud profile.
- **Anything needing fast animation.** The panel refreshes in about a second; partial refresh handles a cursor or a checkbox, not motion.

## Plumbing

- Keep the web server up after Wi-Fi setup; add mDNS (`pockle.local`).
- Profile document applied at boot, migrating today's preferences into it.
- Parent page served from the data partition (the firmware slot is about 69% full; the data partition has over 5 MB free), updated by the same sync that pulls devotionals.
- Parental PIN before any on-device setting.
- Cloud profile service: accounts, pairing codes, hosting. Only if remote or multi-board setup is demanded.
