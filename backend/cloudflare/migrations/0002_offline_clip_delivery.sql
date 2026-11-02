ALTER TABLE direct_messages ADD COLUMN object_key TEXT;
ALTER TABLE direct_messages ADD COLUMN delivery_status TEXT
  CHECK (delivery_status IN ('uploading', 'available', 'expired', 'failed'));
ALTER TABLE direct_messages ADD COLUMN expires_at TEXT;
ALTER TABLE direct_messages ADD COLUMN read_at TEXT;

CREATE TABLE IF NOT EXISTS clip_uploads (
    id TEXT PRIMARY KEY,
    sender_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    recipient_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    object_key TEXT NOT NULL UNIQUE,
    r2_upload_id TEXT,
    clip_name TEXT NOT NULL,
    clip_size INTEGER NOT NULL CHECK (clip_size > 0),
    clip_sha256 TEXT NOT NULL,
    clip_mime TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    expires_at TEXT NOT NULL,
    CHECK (sender_id <> recipient_id)
);

CREATE INDEX IF NOT EXISTS clip_uploads_sender_created
    ON clip_uploads(sender_id, created_at DESC);
