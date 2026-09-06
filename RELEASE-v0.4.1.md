# v0.4.1 — separate inboxes and readable Skyper text

- Three home-screen folders: Nachrichten (64), Wetter/Pegel (32), Warnmeldungen (16).
- Home UP/DOWN selects a folder, ENTER opens it, long ENTER returns home.
- Weather updates quietly replace entries with the same Skyper rubric/item.
- Warning broadcasts with identical text on the same RIC are deduplicated.
- Folder-local deletion and independent persistence preserve personal messages.
- German Skyper characters render as UTF-8 in the terminal and ASCII
  transliterations on the OLED. Unsupported controls become spaces instead of
  causing the entire otherwise readable message to be dropped.
- Raw terminal lines are explicitly labelled Raw.
- Existing battery, GPIO33 radio clock and receive-buffer bounds fix retained.

Default routes follow the observed feed: weather rubrics 61/63/80 and RIC1080;
warning rubric39 and RIC1040. All routes are configurable. Warning sound defaults
off pending verification of the feed; the warning screen and LED reminder work.
Unknown Skyper rubrics remain terminal-only unless SKYPER_NEWS_INBOX is enabled.

No filesystem upload or erase is required. Existing inbox records remain in
Nachrichten. Weather persistence is batched every 30 seconds when reception
permits; a sudden power loss can lose those pending updates.

Validation: host tests cover folder isolation, replacement, deduplication,
deleted-slot reuse, navigation, special characters, time, battery filtering and
the actual patched RadioLib receive bounds. Hardware verification completed on
the LILYGO T3 V1.6.1: clean boot, 4.15 V battery readings, folder navigation,
RIC 1080 and Skyper weather routing, and RIC 208 time reception were observed.
