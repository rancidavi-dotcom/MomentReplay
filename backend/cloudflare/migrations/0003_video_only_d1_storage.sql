-- The remote D1 database was checked before this migration: it contained no
-- users, text messages, clips, or in-progress uploads. Replace the unused
-- message/R2 schema with short-lived D1-backed clip delivery.
DROP TABLE IF EXISTS clip_uploads;
DROP TABLE IF EXISTS direct_messages;

CREATE TABLE shared_clips (
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
CREATE INDEX shared_clips_pair_created
    ON shared_clips(sender_id, recipient_id, created_at DESC);
CREATE INDEX shared_clips_recipient_created
    ON shared_clips(recipient_id, read_at, expires_at);

CREATE TABLE clip_uploads (
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
CREATE INDEX clip_uploads_sender_created
    ON clip_uploads(sender_id, created_at DESC);

CREATE TABLE clip_parts (
    clip_id TEXT NOT NULL,
    part_number INTEGER NOT NULL CHECK (part_number > 0),
    bytes BLOB NOT NULL,
    byte_size INTEGER NOT NULL CHECK (byte_size > 0 AND byte_size <= 1500000),
    PRIMARY KEY (clip_id, part_number)
);
