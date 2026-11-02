# Replay engine: measured issue and implementation stages

## Evidence from the F8 measurement

See [frametime-measurement-2026-11-01.md](frametime-measurement-2026-11-01.md). During the 29.79-second observation after F8, Moment averaged 411% CPU. The saved clip was H.264 `yuv444p`, 78.25 Mbit/s, 147,372,992 bytes for 15.07 seconds. Exact game frametime was not available because MangoHud was not loaded into that running game.

## Current export path

On Linux, replay frames are prepared asynchronously and passed to an FFmpeg child process. The child is limited to one video encoder thread, reads at capture rate, uses `libx264` `ultrafast`, outputs `yuv420p`, and receives an explicit bitrate. Buffered PCM is written to a temporary WAV and encoded to AAC. Output goes to a partial filename and is renamed only after FFmpeg succeeds. The child inherits Moment's low CPU scheduling priority.

This is an interim reduction of the export burst. It still re-encodes video after F8; it is **not yet equivalent to Medal's packet-buffer design**. Temporary frames/audio are created outside the library and removed on completion or failure. Windows keeps the existing Qt Multimedia path.

## Next architecture stage

Replace the Linux JPEG replay ring with a bounded, timestamped ring of encoded H.264 packets produced continuously during capture. Prefer the hardware encoder when available, with a constrained software fallback. On F8, choose a keyframe-aligned time range and mux the existing video packets with AAC audio, without re-encoding the video. Keep the current encoder as a fallback until the packet path has been exercised with real audio, long sessions, resolution changes, buffer wrap, and failure recovery.

## Verification state

- Compilation: `cmake --build build -j2 --target medal-linux` succeeded after the interim change.
- Runtime attempt (2026-11-01): the new build was started hidden while Rocket League stayed focused. One synthetic F8 was sent, but no shortcut activation was observed, no FFmpeg child appeared, and no new clip was created. The attempted export path therefore remains unverified; do not interpret its stable CPU sample as an export benchmark. The cause of the shortcut event not arriving is unknown.
- Automated export test: `instant-replay-test` built and passed (4 passed, 0 failed). It created and decoded a synthetic H.264 clip, then a synthetic H.264 + AAC clip. FFmpeg output used 4:2:0 (`yuvj420p` for JPEG-range input) and AAC 128 kbit/s. This validates the basic export/file path, not in-game frametime or global shortcut delivery.
- A second hidden-game attempt still produced no new clip. The portal monitor saw KDE's `save-replay` release and XDG `Deactivated`, but no captured `Activated` event; no FFmpeg child or recent media file appeared. The hotkey-to-export runtime path remains unresolved.
- Shortcut receiver fix: subscribe to the portal's public owner with explicit D-Bus signature `osta{sv}`, then require the exact active session path before dispatch. The previous implicit slot-signature subscription was accepted by Qt but did not deliver the portal activation. The build compiles. The game lost focus before this build could be exercised with a real F8, so delivery is not yet marked runtime-verified.
- The hidden new-build process remains running; the game was not closed or restarted.
- Game frametime comparison: pending a future game launch with MangoHud enabled.
