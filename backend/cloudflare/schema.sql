-- D1 schema for Moment friend discovery and short-lived private video sharing.
-- Clip bytes are stored as small BLOB chunks and deleted after 24 hours.

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY,
    username TEXT NOT NULL COLLATE NOCASE UNIQUE,
    friend_code TEXT NOT NULL COLLATE NOCASE UNIQUE,
    auth_key_hash TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    disabled INTEGER NOT NULL DEFAULT 0 CHECK (disabled IN (0, 1))
);

CREATE TABLE IF NOT EXISTS friend_requests (
    id TEXT PRIMARY KEY,
    sender_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    recipient_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    status TEXT NOT NULL DEFAULT 'pending'
        CHECK (status IN ('pending', 'accepted', 'declined', 'cancelled')),
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    CHECK (sender_id <> recipient_id)
);

CREATE INDEX IF NOT EXISTS friend_requests_recipient_status
    ON friend_requests(recipient_id, status, created_at DESC);
CREATE INDEX IF NOT EXISTS friend_requests_sender_status
    ON friend_requests(sender_id, status, created_at DESC);

CREATE TABLE IF NOT EXISTS friendships (
    user_low TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    user_high TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    PRIMARY KEY (user_low, user_high),
    CHECK (user_low < user_high)
);

CREATE INDEX IF NOT EXISTS friendships_user_high ON friendships(user_high, user_low);

CREATE TABLE IF NOT EXISTS shared_clips (
    id TEXT PRIMARY KEY,
    sender_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    recipient_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    clip_name TEXT NOT NULL,
    clip_size INTEGER NOT NULL CHECK (clip_size > 0 AND clip_size <= 20971520),
    clip_sha256 TEXT NOT NULL,
    clip_mime TEXT NOT NULL,
    delivery_status TEXT NOT NULL CHECK (delivery_status IN ('available', 'expired')),
    expires_at TEXT NOT NULL,
    read_at TEXT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    CHECK (sender_id <> recipient_id)
);

CREATE INDEX IF NOT EXISTS shared_clips_pair_created
    ON shared_clips(sender_id, recipient_id, created_at DESC);
CREATE INDEX IF NOT EXISTS shared_clips_recipient_created
    ON shared_clips(recipient_id, read_at, expires_at);

CREATE TABLE IF NOT EXISTS clip_uploads (
    id TEXT PRIMARY KEY,
    sender_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    recipient_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    clip_name TEXT NOT NULL,
    clip_size INTEGER NOT NULL CHECK (clip_size > 0 AND clip_size <= 20971520),
    clip_sha256 TEXT NOT NULL,
    clip_mime TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    expires_at TEXT NOT NULL,
    CHECK (sender_id <> recipient_id)
);

CREATE INDEX IF NOT EXISTS clip_uploads_sender_created
    ON clip_uploads(sender_id, created_at DESC);

CREATE TABLE IF NOT EXISTS clip_parts (
    clip_id TEXT NOT NULL,
    part_number INTEGER NOT NULL CHECK (part_number > 0),
    bytes BLOB NOT NULL,
    byte_size INTEGER NOT NULL CHECK (byte_size > 0 AND byte_size <= 1500000),
    PRIMARY KEY (clip_id, part_number)
);
