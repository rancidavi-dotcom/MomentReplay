# Moment Friends Worker

Cloudflare Worker API for Moment accounts, friend requests, presence, and temporary video sharing. The service has no text-chat feature and stores no text-message bodies. It uses D1 only; no R2 subscription or bucket is required.

## Video limits

- Up to 20 MiB per video.
- Up to 100 MiB of active and in-progress clips across the service.
- 1,500,000-byte chunks are stored in D1 BLOB rows.
- Completed videos expire after 24 hours; abandoned uploads after one hour.
- Expired chunks are removed hourly and before a new upload reserves space.
- Retrieval requires an authenticated sender/recipient relationship.

The sender's original remains local. A recipient can download while online or later within the retention window. D1 is not a media store, so this design intentionally accepts small clips and limited service capacity. Cloudflare Free quotas are hard stops; when they are exhausted requests may fail until the quota resets or space is reclaimed.

## Components

- D1: accounts, friend requests, friendships, clip metadata, upload reservations, and video chunks.
- Durable Object: online presence and real-time friend/clip notifications.
- Qt client: account keychain, friend list, upload/download, integrity verification, and popups.

Account auth keys are generated on the client. The Worker stores only their SHA-256 digest. Clip bytes and metadata are not end-to-end encrypted; use this as a private beta service until privacy disclosures, account deletion, abuse controls, and security review are complete.

## Local setup

1. Copy `wrangler.example.jsonc` to `wrangler.jsonc` and set the D1 ID.
2. Run `npm ci` in this directory.
3. For a new database, apply `schema.sql`. For the existing `moment-social` database, apply migrations in order; migration 0003 replaces unused text/R2 tables with video-only D1 tables.
4. Deploy with Wrangler after configuring a Cloudflare API token limited to Workers Scripts and D1. This application does not bind R2.
5. Build Moment with `-DMOMENT_FRIENDS_API_URL=https://your-worker.example.workers.dev`.

## API outline

All non-registration/login routes use `Authorization: Bearer <64 hex chars>`.

| Method | Route | Purpose |
| --- | --- | --- |
| GET | `/api/v1/health` | Health check |
| POST | `/api/v1/accounts/register` | Create a Moment account |
| POST | `/api/v1/accounts/login` | Validate an account key |
| GET | `/api/v1/me` | Current account |
| POST | `/api/v1/friends/requests` | Add by friend code |
| GET | `/api/v1/friends` | Friends and pending requests |
| POST | `/api/v1/friends/requests/{id}/accept` | Accept a friend request |
| POST | `/api/v1/friends/requests/{id}/decline` | Decline a friend request |
| GET | `/api/v1/clips?with={friendId}` | List shared videos with a friend |
| GET | `/api/v1/inbox` | List videos received while away |
| POST | `/api/v1/clips/ack` | Acknowledge video notifications |
| POST | `/api/v1/clips/uploads` | Reserve a video upload |
| PUT | `/api/v1/clips/uploads/{id}/parts/{n}` | Store a video chunk |
| POST | `/api/v1/clips/uploads/{id}` | Finalize and notify the recipient |
| GET | `/api/v1/clips/{id}/parts/{n}` | Download a video chunk |
| DELETE | `/api/v1/clips/{id}` | Delete a shared video |
| GET | `/api/v1/realtime` | Authenticated WebSocket |

The API intentionally has no `/messages` route. Do not commit account tokens, recovery keys, or user data.
