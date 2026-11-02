# Moment Friends: status

## Product scope

Friends supports accounts, friend codes, friend requests, online presence, and temporary video sharing. Text chat has been removed from the desktop UI and Worker API. The service stores only friend/clip metadata and video chunks; video bytes are deleted after 24 hours.

## Free storage limits

- Maximum video: 20 MiB.
- Maximum total active and in-progress video data: 100 MiB across the service.
- D1 chunk size: 1,500,000 bytes per row, below the D1 2 MB value limit.
- Expiry: 24 hours after upload completion; abandoned uploads expire after one hour.
- The sender keeps the original file locally. The recipient saves a verified copy under `Videos/Moment/Received`.
- When the 100 MiB service cap is reached, new sends are rejected until clips expire or users delete them.

This design uses the Cloudflare Workers Free and D1 Free quotas. It is intentionally capacity-limited and can stop accepting requests if Cloudflare daily quotas are exhausted. It does not use R2 or require an R2 subscription.

## Implemented locally

- C++ client: friend accounts, immediate mutual friendship by friend code, friend selection, asynchronous upload, chunked download, SHA-256 verification, inbox polling, WebSocket clip notifications, bounded reconnect backoff, and desktop popups.
- QML page: friend list and presence, friend codes, share-video action, received clips, expiration time, download progress, and confirmed deletion of shared clips for both chat views. No text composer or text conversation.
- Worker source: D1-backed clip chunks, quota reservation, authenticated retrieval, delete, and hourly expiration cleanup. No R2 binding or text-message endpoint.
- Local integration passed with two synthetic accounts and a 1.7 MB synthetic video split into two D1 chunks. Retrieved bytes matched the uploaded SHA-256.
- Remote integration passed against the deployed Worker with two temporary synthetic accounts and a 1.65 MB synthetic video. Downloaded bytes matched the uploaded SHA-256; test accounts and clip were deleted afterwards.
- Linux application rebuilt with `MOMENT_FRIENDS_API_URL` set to the deployed Worker.

## Cloudflare account state

- `moment-social` exists. Its prior schema contained the 0002 columns, while `d1_migrations` had no history records; 0002 was recorded in migration history to match that verified schema.
- Aggregate counts were zero immediately before the schema replacement. Migration 0003 is now applied; it dropped the unused `direct_messages` and old R2-oriented upload tables and created D1 chunk storage.
- The migration list reports no pending migrations. The text-message table and `/messages` API are absent.
- R2 remains disabled. No R2 subscription was accepted.
- Worker deployed at `https://moment-friends-api.rancidavi.workers.dev`, version `2936b90d-f7c5-4170-8b70-53df4d144e93`; `/api/v1/health` returned `ok`. This deployment preserves Cloudflare's WebSocket upgrade response, so the realtime socket can establish correctly.
- The local `build/medal-linux` binary was rebuilt with the deployed API URL. The Friends flow still needs the user to create/sign in to an account and try it in the UI.

## Remaining work

1. Exercise account creation, friend requests, and a video transfer from two Moment installations.
2. Add account deletion and abuse/rate controls before advertising the service broadly.
3. Review account/recovery-key handling and privacy disclosures before inviting more users.

Friends popups can only appear while Moment is running. If the recipient is offline, they see the clip after opening Moment, provided the 24-hour retention window has not expired.
