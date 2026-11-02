const API_PREFIX = "/api/v1";
const MAX_JSON_BYTES = 96 * 1024;
const MAX_SIGNAL_BYTES = 64 * 1024;
const CLIP_PART_BYTES = 1_500_000;
const MAX_CLIP_BYTES = 20 * 1024 * 1024;
const MAX_SERVICE_CLIP_BYTES = 100 * 1024 * 1024;
const CLIP_RETENTION_MS = 24 * 60 * 60 * 1000;
const UPLOAD_RETENTION_MS = 60 * 60 * 1000;
const FRIEND_CODE_ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
const SIGNAL_TYPES = new Set([
  "offer",
  "answer",
  "ice",
  "clip-accepted",
  "clip-declined",
  "clip-progress",
  "clip-complete",
  "clip-failed",
]);

function json(data, status = 200, extraHeaders = {}) {
  return new Response(JSON.stringify(data), {
    status,
    headers: {
      "content-type": "application/json; charset=utf-8",
      "cache-control": "no-store",
      "access-control-allow-origin": "*",
      "access-control-allow-methods": "GET,POST,PUT,DELETE,OPTIONS",
      "access-control-allow-headers": "authorization,content-type",
      ...extraHeaders,
    },
  });
}

function error(message, status = 400, code = "bad_request") {
  return json({ error: { code, message } }, status);
}

function withCors(response) {
  const headers = new Headers(response.headers);
  headers.set("access-control-allow-origin", "*");
  headers.set("access-control-allow-methods", "GET,POST,PUT,DELETE,OPTIONS");
  headers.set("access-control-allow-headers", "authorization,content-type");
  headers.set("access-control-max-age", "86400");
  return new Response(response.body, { status: response.status, headers });
}

async function readJson(request) {
  const length = Number(request.headers.get("content-length") || 0);
  if (length > MAX_JSON_BYTES) throw new HttpError(413, "Request body is too large.");
  const raw = await request.text();
  if (new TextEncoder().encode(raw).byteLength > MAX_JSON_BYTES)
    throw new HttpError(413, "Request body is too large.");
  try {
    const parsed = JSON.parse(raw || "{}");
    if (!parsed || Array.isArray(parsed) || typeof parsed !== "object")
      throw new Error("Expected object");
    return parsed;
  } catch {
    throw new HttpError(400, "Request body must be a JSON object.");
  }
}

class HttpError extends Error {
  constructor(status, message, code = "bad_request") {
    super(message);
    this.status = status;
    this.code = code;
  }
}

function normalizeUsername(value) {
  if (typeof value !== "string") throw new HttpError(400, "Username is required.");
  const username = value.trim();
  if (!/^[A-Za-z0-9_]{3,24}$/.test(username))
    throw new HttpError(400, "Username must be 3–24 letters, numbers, or underscores.");
  return username;
}

function normalizeAuthKey(value) {
  if (typeof value !== "string" || !/^[a-f0-9]{64}$/i.test(value))
    throw new HttpError(400, "A 256-bit account key is required.");
  return value.toLowerCase();
}

async function sha256Hex(value) {
  const bytes = new TextEncoder().encode(value);
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(digest)].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

function randomFriendCode() {
  const bytes = crypto.getRandomValues(new Uint8Array(8));
  return [...bytes].map((byte) => FRIEND_CODE_ALPHABET[byte % FRIEND_CODE_ALPHABET.length]).join("");
}

function publicUser(row) {
  return {
    id: row.id,
    username: row.username,
    friendCode: row.friend_code,
    createdAt: row.created_at,
  };
}

async function authenticate(request, env) {
  const match = /^Bearer\s+([a-f0-9]{64})$/i.exec(request.headers.get("authorization") || "");
  if (!match) throw new HttpError(401, "Sign in to Moment Friends first.", "unauthorized");
  const keyHash = await sha256Hex(match[1].toLowerCase());
  const user = await env.DB.prepare(
    "SELECT id, username, friend_code, created_at, disabled FROM users WHERE auth_key_hash = ? LIMIT 1",
  ).bind(keyHash).first();
  if (!user || user.disabled)
    throw new HttpError(401, "This Moment Friends key is invalid or disabled.", "unauthorized");
  return user;
}

function pairIds(left, right) {
  return left < right ? [left, right] : [right, left];
}

async function areFriends(env, left, right) {
  const [low, high] = pairIds(left, right);
  return Boolean(await env.DB.prepare(
    "SELECT 1 AS found FROM friendships WHERE user_low = ? AND user_high = ? LIMIT 1",
  ).bind(low, high).first());
}

async function notifyUser(env, userId, event) {
  const id = env.REALTIME.idFromName(userId);
  const hub = env.REALTIME.get(id);
  await hub.fetch("https://moment-internal/deliver", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(event),
  });
}

async function cleanupExpired(env) {
  const now = new Date().toISOString();
  const [clips, uploads] = await Promise.all([
    env.DB.prepare("SELECT id FROM shared_clips WHERE expires_at <= ? LIMIT 100").bind(now).all(),
    env.DB.prepare("SELECT id FROM clip_uploads WHERE expires_at <= ? LIMIT 100").bind(now).all(),
  ]);
  for (const clip of clips.results || []) {
    await env.DB.batch([
      env.DB.prepare("DELETE FROM clip_parts WHERE clip_id = ?").bind(clip.id),
      env.DB.prepare("DELETE FROM shared_clips WHERE id = ?").bind(clip.id),
    ]);
  }
  for (const upload of uploads.results || []) {
    await env.DB.batch([
      env.DB.prepare("DELETE FROM clip_parts WHERE clip_id = ?").bind(upload.id),
      env.DB.prepare("DELETE FROM clip_uploads WHERE id = ?").bind(upload.id),
    ]);
  }
}

async function handleApi(request, env, path) {
  const method = request.method;

  if (method === "GET" && path === "/health")
    return json({ service: "moment-friends", status: "ok", version: 1 });

  if (method === "POST" && path === "/accounts/register") {
    const body = await readJson(request);
    const username = normalizeUsername(body.username);
    const authKey = normalizeAuthKey(body.authKey);
    const authKeyHash = await sha256Hex(authKey);
    const userId = crypto.randomUUID();
    let friendCode = "";
    for (let attempt = 0; attempt < 5; attempt++) {
      const candidate = randomFriendCode();
      const used = await env.DB.prepare("SELECT 1 FROM users WHERE friend_code = ? LIMIT 1")
        .bind(candidate).first();
      if (!used) { friendCode = candidate; break; }
    }
    if (!friendCode) throw new HttpError(503, "Could not allocate a friend code. Try again.", "temporary_failure");
    try {
      await env.DB.prepare(
        "INSERT INTO users (id, username, friend_code, auth_key_hash) VALUES (?, ?, ?, ?)",
      ).bind(userId, username, friendCode, authKeyHash).run();
    } catch (cause) {
      const message = String(cause?.message || "");
      if (/username/i.test(message) || /UNIQUE constraint failed: users.username/i.test(message))
        throw new HttpError(409, "That username is already in use.", "username_taken");
      if (/auth_key_hash/i.test(message))
        throw new HttpError(409, "This account key is already registered.", "key_in_use");
      throw cause;
    }
    const row = await env.DB.prepare(
      "SELECT id, username, friend_code, created_at FROM users WHERE id = ?",
    ).bind(userId).first();
    return json({ user: publicUser(row) }, 201);
  }

  if (method === "POST" && path === "/accounts/login") {
    const body = await readJson(request);
    const username = normalizeUsername(body.username);
    const authKeyHash = await sha256Hex(normalizeAuthKey(body.authKey));
    const user = await env.DB.prepare(
      "SELECT id, username, friend_code, created_at, disabled FROM users WHERE username = ? COLLATE NOCASE AND auth_key_hash = ? LIMIT 1",
    ).bind(username, authKeyHash).first();
    if (!user || user.disabled)
      throw new HttpError(401, "Username or recovery key is incorrect.", "invalid_credentials");
    return json({ user: publicUser(user) });
  }

  const user = await authenticate(request, env);

  if (method === "GET" && path === "/me")
    return json({ user: publicUser(user) });

  if (method === "POST" && path === "/accounts/rotate-key") {
    const body = await readJson(request);
    const nextKeyHash = await sha256Hex(normalizeAuthKey(body.authKey));
    if (nextKeyHash === await sha256Hex((request.headers.get("authorization") || "").slice(7)))
      throw new HttpError(400, "Choose a different account key.");
    await env.DB.prepare("UPDATE users SET auth_key_hash = ? WHERE id = ?")
      .bind(nextKeyHash, user.id).run();
    return json({ ok: true });
  }

  if (method === "POST" && path === "/friends/requests") {
    const body = await readJson(request);
    const code = typeof body.friendCode === "string" ? body.friendCode.trim().toUpperCase() : "";
    if (!/^[A-HJ-NP-Z2-9]{8}$/.test(code)) throw new HttpError(400, "Enter a valid 8-character friend code.");
    const recipient = await env.DB.prepare(
      "SELECT id, username, friend_code, created_at FROM users WHERE friend_code = ? COLLATE NOCASE AND disabled = 0 LIMIT 1",
    ).bind(code).first();
    if (!recipient) throw new HttpError(404, "No Moment account uses that friend code.", "friend_not_found");
    if (recipient.id === user.id) throw new HttpError(400, "You cannot add yourself.");
    if (await areFriends(env, user.id, recipient.id))
      throw new HttpError(409, "You are already friends.", "already_friends");
    const pending = await env.DB.prepare(
      "SELECT id, sender_id, recipient_id FROM friend_requests WHERE status = 'pending' AND ((sender_id = ? AND recipient_id = ?) OR (sender_id = ? AND recipient_id = ?)) LIMIT 1",
    ).bind(user.id, recipient.id, recipient.id, user.id).first();
    if (pending) {
      if (pending.sender_id === recipient.id && pending.recipient_id === user.id) {
        const [low, high] = pairIds(user.id, recipient.id);
        await env.DB.batch([
          env.DB.prepare("UPDATE friend_requests SET status = 'accepted', updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = ?")
            .bind(pending.id),
          env.DB.prepare("INSERT OR IGNORE INTO friendships (user_low, user_high) VALUES (?, ?)").bind(low, high),
        ]);
        await notifyUser(env, recipient.id, { type: "friend.accepted", friend: publicUser(user) });
        return json({ status: "accepted", friend: publicUser(recipient) });
      }
      return json({ status: "pending", requestId: pending.id, friend: publicUser(recipient) }, 200);
    }
    const requestId = crypto.randomUUID();
    await env.DB.prepare(
      "INSERT INTO friend_requests (id, sender_id, recipient_id) VALUES (?, ?, ?)",
    ).bind(requestId, user.id, recipient.id).run();
    await notifyUser(env, recipient.id, { type: "friend.request", requestId, user: publicUser(user) });
    return json({ status: "pending", requestId, friend: publicUser(recipient) }, 201);
  }

  if (method === "GET" && path === "/friends") {
    const [lowSide, highSide, received, sent] = await Promise.all([
      env.DB.prepare(
        "SELECT u.id, u.username, u.friend_code, u.created_at FROM friendships f JOIN users u ON u.id = f.user_high WHERE f.user_low = ? AND u.disabled = 0 ORDER BY u.username COLLATE NOCASE",
      ).bind(user.id).all(),
      env.DB.prepare(
        "SELECT u.id, u.username, u.friend_code, u.created_at FROM friendships f JOIN users u ON u.id = f.user_low WHERE f.user_high = ? AND u.disabled = 0 ORDER BY u.username COLLATE NOCASE",
      ).bind(user.id).all(),
      env.DB.prepare(
        "SELECT r.id, r.created_at, u.id AS user_id, u.username, u.friend_code, u.created_at AS user_created_at FROM friend_requests r JOIN users u ON u.id = r.sender_id WHERE r.recipient_id = ? AND r.status = 'pending' ORDER BY r.created_at DESC LIMIT 100",
      ).bind(user.id).all(),
      env.DB.prepare(
        "SELECT r.id, r.created_at, u.id AS user_id, u.username, u.friend_code, u.created_at AS user_created_at FROM friend_requests r JOIN users u ON u.id = r.recipient_id WHERE r.sender_id = ? AND r.status = 'pending' ORDER BY r.created_at DESC LIMIT 100",
      ).bind(user.id).all(),
    ]);
    const friends = [...(lowSide.results || []), ...(highSide.results || [])];
    const presence = await Promise.all(friends.map(async (friend) => {
      try {
        const id = env.REALTIME.idFromName(friend.id);
        const response = await env.REALTIME.get(id).fetch("https://moment-internal/presence");
        return response.ok && (await response.json()).online;
      } catch { return false; }
    }));
    return json({
      friends: friends.map((friend, index) => ({ ...publicUser(friend), online: Boolean(presence[index]) })),
      incoming: (received.results || []).map((row) => ({
        requestId: row.id, createdAt: row.created_at,
        user: { id: row.user_id, username: row.username, friendCode: row.friend_code, createdAt: row.user_created_at },
      })),
      outgoing: (sent.results || []).map((row) => ({
        requestId: row.id, createdAt: row.created_at,
        user: { id: row.user_id, username: row.username, friendCode: row.friend_code, createdAt: row.user_created_at },
      })),
    });
  }

  const requestMatch = /^\/friends\/requests\/([0-9a-f-]{36})\/(accept|decline)$/.exec(path);
  if (method === "POST" && requestMatch) {
    const [, requestId, action] = requestMatch;
    const friendRequest = await env.DB.prepare(
      "SELECT id, sender_id, recipient_id, status FROM friend_requests WHERE id = ? AND recipient_id = ? LIMIT 1",
    ).bind(requestId, user.id).first();
    if (!friendRequest) throw new HttpError(404, "Friend request not found.", "request_not_found");
    if (friendRequest.status !== "pending") throw new HttpError(409, "This friend request is no longer pending.");
    if (action === "decline") {
      await env.DB.prepare("UPDATE friend_requests SET status = 'declined', updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = ?")
        .bind(requestId).run();
      await notifyUser(env, friendRequest.sender_id, { type: "friend.declined", requestId });
      return json({ status: "declined" });
    }
    const [low, high] = pairIds(user.id, friendRequest.sender_id);
    await env.DB.batch([
      env.DB.prepare("UPDATE friend_requests SET status = 'accepted', updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = ?")
        .bind(requestId),
      env.DB.prepare("INSERT OR IGNORE INTO friendships (user_low, user_high) VALUES (?, ?)").bind(low, high),
    ]);
    const friend = await env.DB.prepare("SELECT id, username, friend_code, created_at FROM users WHERE id = ?")
      .bind(friendRequest.sender_id).first();
    await notifyUser(env, friend.id, { type: "friend.accepted", friend: publicUser(user) });
    return json({ status: "accepted", friend: publicUser(friend) });
  }

  const friendMatch = /^\/friends\/([0-9a-f-]{36})$/.exec(path);
  if (method === "DELETE" && friendMatch) {
    const friendId = friendMatch[1];
    const [low, high] = pairIds(user.id, friendId);
    await env.DB.batch([
      env.DB.prepare("DELETE FROM friendships WHERE user_low = ? AND user_high = ?").bind(low, high),
      env.DB.prepare("UPDATE friend_requests SET status = 'cancelled', updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE status IN ('pending', 'accepted') AND ((sender_id = ? AND recipient_id = ?) OR (sender_id = ? AND recipient_id = ?))")
        .bind(user.id, friendId, friendId, user.id),
    ]);
    return json({ ok: true });
  }

  if (method === "GET" && path === "/clips") {
    const friendId = new URL(request.url).searchParams.get("with") || "";
    if (!/^[0-9a-f-]{36}$/.test(friendId)) throw new HttpError(400, "A friend id is required.");
    if (!(await areFriends(env, user.id, friendId))) throw new HttpError(403, "Videos are available to friends only.", "not_friends");
    const result = await env.DB.prepare(
      "SELECT id, sender_id, recipient_id, clip_name, clip_size, clip_sha256, clip_mime, delivery_status, expires_at, read_at, created_at FROM shared_clips WHERE ((sender_id = ? AND recipient_id = ?) OR (sender_id = ? AND recipient_id = ?)) ORDER BY created_at DESC LIMIT 50",
    ).bind(user.id, friendId, friendId, user.id).all();
    return json({ clips: (result.results || []).reverse() });
  }

  if (method === "GET" && path === "/inbox") {
    const result = await env.DB.prepare(
      "SELECT c.id, c.sender_id, c.recipient_id, c.clip_name, c.clip_size, c.clip_sha256, c.clip_mime, c.delivery_status, c.expires_at, c.created_at, u.username AS sender_name FROM shared_clips c JOIN users u ON u.id = c.sender_id WHERE c.recipient_id = ? AND c.read_at IS NULL AND c.expires_at > strftime('%Y-%m-%dT%H:%M:%fZ', 'now') ORDER BY c.created_at ASC LIMIT 100",
    ).bind(user.id).all();
    return json({ clips: result.results || [] });
  }

  if (method === "POST" && path === "/clips/ack") {
    const body = await readJson(request);
    const ids = Array.isArray(body.ids) ? [...new Set(body.ids)] : [];
    if (ids.length > 100 || ids.some((id) => typeof id !== "string" || !/^[0-9a-f-]{36}$/.test(id)))
      throw new HttpError(400, "Video acknowledgement list is invalid.");
    if (ids.length) {
      const placeholders = ids.map(() => "?").join(",");
      await env.DB.prepare(
        `UPDATE shared_clips SET read_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE recipient_id = ? AND read_at IS NULL AND id IN (${placeholders})`,
      ).bind(user.id, ...ids).run();
    }
    return json({ ok: true });
  }

  if (method === "POST" && path === "/clips/uploads") {
    const body = await readJson(request);
    const recipientId = typeof body.to === "string" ? body.to : "";
    if (!/^[0-9a-f-]{36}$/.test(recipientId)) throw new HttpError(400, "A friend id is required.");
    if (!(await areFriends(env, user.id, recipientId))) throw new HttpError(403, "Clips can only be sent to friends.", "not_friends");
    const clipName = typeof body.clipName === "string" ? body.clipName.replace(/[\\/\0-\x1f]/g, "_").trim().slice(0, 160) : "";
    const size = Number(body.clipSize);
    const digest = typeof body.clipSha256 === "string" ? body.clipSha256.toLowerCase() : "";
    const mime = typeof body.clipMime === "string" ? body.clipMime.toLowerCase() : "";
    if (!clipName || !Number.isSafeInteger(size) || size <= 0 || size > MAX_CLIP_BYTES || !/^[a-f0-9]{64}$/.test(digest))
      throw new HttpError(400, `Choose a video up to ${MAX_CLIP_BYTES / (1024 * 1024)} MiB with a SHA-256 digest.`);
    if (!["video/mp4", "video/webm", "video/quicktime", "video/x-matroska"].includes(mime))
      throw new HttpError(400, "Supported video types are MP4, WebM, MOV, and Matroska.");

    await cleanupExpired(env);
    const uploadId = crypto.randomUUID();
    const expiresAt = new Date(Date.now() + UPLOAD_RETENTION_MS).toISOString();
    const reserve = await env.DB.prepare(
      `INSERT INTO clip_uploads (id, sender_id, recipient_id, clip_name, clip_size, clip_sha256, clip_mime, expires_at)
       SELECT ?, ?, ?, ?, ?, ?, ?, ?
       WHERE (SELECT COALESCE(SUM(clip_size), 0) FROM shared_clips WHERE delivery_status = 'available' AND expires_at > strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
         + (SELECT COALESCE(SUM(clip_size), 0) FROM clip_uploads WHERE expires_at > strftime('%Y-%m-%dT%H:%M:%fZ', 'now')) + ? <= ?`,
    ).bind(uploadId, user.id, recipientId, clipName, size, digest, mime, expiresAt,
      size, MAX_SERVICE_CLIP_BYTES).run();
    if (!reserve.meta?.changes)
      throw new HttpError(507, "Moment Friends storage is at its free-service limit. Remove old clips or try later.", "storage_quota");

    return json({ uploadId, partSize: CLIP_PART_BYTES, expiresInSeconds: UPLOAD_RETENTION_MS / 1000 }, 201);
  }

  const partMatch = /^\/clips\/uploads\/([0-9a-f-]{36})\/parts\/([0-9]{1,5})$/.exec(path);
  if (method === "PUT" && partMatch) {
    const [, uploadId, numberText] = partMatch;
    const partNumber = Number(numberText);
    const upload = await env.DB.prepare(
      "SELECT clip_size FROM clip_uploads WHERE id = ? AND sender_id = ? AND expires_at > strftime('%Y-%m-%dT%H:%M:%fZ', 'now') LIMIT 1",
    ).bind(uploadId, user.id).first();
    if (!upload) throw new HttpError(404, "Clip upload was not found or has expired.", "upload_not_found");
    const count = Math.ceil(upload.clip_size / CLIP_PART_BYTES);
    const expectedSize = partNumber < count ? CLIP_PART_BYTES : upload.clip_size - CLIP_PART_BYTES * (count - 1);
    const declaredSize = Number(request.headers.get("content-length") || 0);
    if (!Number.isInteger(partNumber) || partNumber < 1 || partNumber > count
      || (declaredSize && declaredSize !== expectedSize))
      throw new HttpError(400, "Clip part number or size is invalid.", "invalid_part");
    const bytes = new Uint8Array(await request.arrayBuffer());
    if (bytes.byteLength !== expectedSize) throw new HttpError(400, "Clip part byte count is invalid.", "invalid_part");
    await env.DB.prepare(
      "INSERT INTO clip_parts (clip_id, part_number, bytes, byte_size) VALUES (?, ?, ?, ?) ON CONFLICT(clip_id, part_number) DO UPDATE SET bytes = excluded.bytes, byte_size = excluded.byte_size",
    ).bind(uploadId, partNumber, bytes, bytes.byteLength).run();
    return json({ partNumber, size: bytes.byteLength });
  }

  const uploadMatch = /^\/clips\/uploads\/([0-9a-f-]{36})$/.exec(path);
  if (method === "POST" && uploadMatch) {
    const uploadId = uploadMatch[1];
    const upload = await env.DB.prepare(
      "SELECT * FROM clip_uploads WHERE id = ? AND sender_id = ? AND expires_at > strftime('%Y-%m-%dT%H:%M:%fZ', 'now') LIMIT 1",
    ).bind(uploadId, user.id).first();
    if (!upload) throw new HttpError(404, "Clip upload was not found or has expired.", "upload_not_found");
    const expectedParts = Math.ceil(upload.clip_size / CLIP_PART_BYTES);
    const actualParts = await env.DB.prepare(
      "SELECT COUNT(*) AS part_count, COALESCE(SUM(byte_size), 0) AS byte_count, MIN(part_number) AS first_part, MAX(part_number) AS last_part FROM clip_parts WHERE clip_id = ?",
    ).bind(uploadId).first();
    if (actualParts.part_count !== expectedParts || actualParts.byte_count !== upload.clip_size
      || actualParts.first_part !== 1 || actualParts.last_part !== expectedParts)
      throw new HttpError(400, "The video upload is incomplete.", "incomplete_upload");
    const expiresAt = new Date(Date.now() + CLIP_RETENTION_MS).toISOString();
    const row = {
      id: uploadId,
      sender_id: user.id,
      recipient_id: upload.recipient_id,
      clip_name: upload.clip_name,
      clip_size: upload.clip_size,
      clip_sha256: upload.clip_sha256,
      clip_mime: upload.clip_mime,
      delivery_status: "available",
      expires_at: expiresAt,
    };
    await env.DB.batch([
      env.DB.prepare(
        "INSERT INTO shared_clips (id, sender_id, recipient_id, clip_name, clip_size, clip_sha256, clip_mime, delivery_status, expires_at) VALUES (?, ?, ?, ?, ?, ?, ?, 'available', ?)",
      ).bind(uploadId, user.id, upload.recipient_id, upload.clip_name, upload.clip_size,
        upload.clip_sha256, upload.clip_mime, expiresAt),
      env.DB.prepare("DELETE FROM clip_uploads WHERE id = ?").bind(uploadId),
    ]);
    await notifyUser(env, upload.recipient_id, { type: "clip.new", clip: row });
    return json({ clip: row }, 201);
  }

  if (method === "DELETE" && uploadMatch) {
    const uploadId = uploadMatch[1];
    const upload = await env.DB.prepare(
      "SELECT id FROM clip_uploads WHERE id = ? AND sender_id = ? LIMIT 1",
    ).bind(uploadId, user.id).first();
    if (upload) {
      await env.DB.batch([
        env.DB.prepare("DELETE FROM clip_parts WHERE clip_id = ?").bind(uploadId),
        env.DB.prepare("DELETE FROM clip_uploads WHERE id = ?").bind(uploadId),
      ]);
    }
    return json({ ok: true });
  }

  const clipPartMatch = /^\/clips\/([0-9a-f-]{36})\/parts\/([0-9]{1,5})$/.exec(path);
  if (method === "GET" && clipPartMatch) {
    const [, clipId, numberText] = clipPartMatch;
    const partNumber = Number(numberText);
    const clip = await env.DB.prepare(
      "SELECT sender_id, recipient_id, clip_size, delivery_status, expires_at FROM shared_clips WHERE id = ? LIMIT 1",
    ).bind(clipId).first();
    if (!clip || (clip.sender_id !== user.id && clip.recipient_id !== user.id)
      || !(await areFriends(env, clip.sender_id, clip.recipient_id)))
      throw new HttpError(404, "Clip was not found.", "clip_not_found");
    if (clip.delivery_status !== "available"
      || Date.parse(clip.expires_at) <= Date.now())
      throw new HttpError(410, "This clip is no longer available.", "clip_expired");
    const partCount = Math.ceil(clip.clip_size / CLIP_PART_BYTES);
    if (!Number.isInteger(partNumber) || partNumber < 1 || partNumber > partCount)
      throw new HttpError(400, "Clip part number is invalid.", "invalid_part");
    const part = await env.DB.prepare(
      "SELECT bytes, byte_size FROM clip_parts WHERE clip_id = ? AND part_number = ? LIMIT 1",
    ).bind(clipId, partNumber).first();
    if (!part) throw new HttpError(410, "This video part is no longer available.", "clip_expired");
    let bytes;
    if (part.bytes instanceof ArrayBuffer) bytes = new Uint8Array(part.bytes);
    else if (ArrayBuffer.isView(part.bytes))
      bytes = new Uint8Array(part.bytes.buffer, part.bytes.byteOffset, part.bytes.byteLength);
    else if (Array.isArray(part.bytes)) bytes = Uint8Array.from(part.bytes);
    else throw new HttpError(500, "Stored video data has an unsupported format.", "invalid_clip_data");
    if (bytes.byteLength !== part.byte_size)
      throw new HttpError(500, "Stored video data is incomplete.", "invalid_clip_data");
    return new Response(bytes, { status: 200, headers: {
      "content-type": "application/octet-stream",
      "content-length": String(part.byte_size),
      "cache-control": "private, no-store",
      "access-control-allow-origin": "*",
      "access-control-allow-methods": "GET,POST,PUT,DELETE,OPTIONS",
      "access-control-allow-headers": "authorization,content-type",
    } });
  }

  const clipMatch = /^\/clips\/([0-9a-f-]{36})$/.exec(path);
  if (method === "DELETE" && clipMatch) {
    const clip = await env.DB.prepare(
      "SELECT sender_id, recipient_id FROM shared_clips WHERE id = ? LIMIT 1",
    ).bind(clipMatch[1]).first();
    if (!clip || (clip.sender_id !== user.id && clip.recipient_id !== user.id))
      throw new HttpError(404, "Clip was not found.", "clip_not_found");
    await env.DB.batch([
      env.DB.prepare("DELETE FROM clip_parts WHERE clip_id = ?").bind(clipMatch[1]),
      env.DB.prepare("DELETE FROM shared_clips WHERE id = ?").bind(clipMatch[1]),
    ]);
    return json({ ok: true });
  }

  if (method === "GET" && path === "/realtime") {
    if (request.headers.get("upgrade")?.toLowerCase() !== "websocket")
      throw new HttpError(426, "WebSocket upgrade required.");
    const id = env.REALTIME.idFromName(user.id);
    const headers = new Headers(request.headers);
    headers.set("x-moment-user-id", user.id);
    return env.REALTIME.get(id).fetch(new Request(request, { headers }));
  }

  return error("Endpoint not found.", 404, "not_found");
}

export default {
  async fetch(request, env) {
    if (request.method === "OPTIONS") return withCors(new Response(null, { status: 204 }));
    const url = new URL(request.url);
    if (!url.pathname.startsWith(`${API_PREFIX}/`)) return error("Endpoint not found.", 404, "not_found");
    try {
      const response = await handleApi(request, env, url.pathname.slice(API_PREFIX.length));
      return withCors(response);
    } catch (cause) {
      if (cause instanceof HttpError) return error(cause.message, cause.status, cause.code);
      console.error("Moment Friends API request failed", { name: cause?.name, message: cause?.message });
      return error("The Moment Friends service could not complete this request.", 500, "internal_error");
    }
  },

  async scheduled(_controller, env, context) {
    context.waitUntil(cleanupExpired(env));
  },
};

export class MomentRealtimeHub {
  constructor(state, env) {
    this.state = state;
    this.env = env;
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (url.pathname === "/presence") return json({ online: this.state.getWebSockets().length > 0 });
    if (url.pathname === "/deliver" && request.method === "POST") {
      const message = await request.json();
      const encoded = JSON.stringify(message);
      for (const socket of this.state.getWebSockets()) {
        try { socket.send(encoded); } catch { /* stale sockets close on the next event */ }
      }
      return new Response(null, { status: 204 });
    }
    if (url.pathname !== "/" || request.headers.get("upgrade")?.toLowerCase() !== "websocket")
      return error("Realtime endpoint not found.", 404, "not_found");
    const userId = request.headers.get("x-moment-user-id");
    if (!userId) return error("Realtime authentication required.", 401, "unauthorized");
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.state.acceptWebSocket(server);
    server.serializeAttachment({ userId });
    return new Response(null, { status: 101, webSocket: client });
  }

  async webSocketMessage(socket, rawMessage) {
    const senderId = socket.deserializeAttachment()?.userId;
    if (!senderId || typeof rawMessage !== "string" || rawMessage.length > MAX_SIGNAL_BYTES) {
      socket.close(1008, "Invalid signaling message");
      return;
    }
    let message;
    try { message = JSON.parse(rawMessage); } catch {
      socket.close(1008, "Invalid JSON");
      return;
    }
    const targetId = typeof message.to === "string" ? message.to : "";
    if (!/^[0-9a-f-]{36}$/.test(targetId) || !SIGNAL_TYPES.has(message.type)) {
      socket.send(JSON.stringify({ type: "error", code: "invalid_signal" }));
      return;
    }
    if (!(await areFriends(this.env, senderId, targetId))) {
      socket.send(JSON.stringify({ type: "error", code: "not_friends" }));
      return;
    }
    const target = this.env.REALTIME.get(this.env.REALTIME.idFromName(targetId));
    await target.fetch("https://moment-internal/deliver", {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ type: "signal", from: senderId, signalType: message.type, payload: message.payload ?? null }),
    });
  }
}
