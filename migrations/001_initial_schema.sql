-- Full initial schema: items, categories, subcategories, timesheet.
-- All statements are idempotent (CREATE TABLE IF NOT EXISTS).
CREATE TABLE IF NOT EXISTS items (
    id          INTEGER PRIMARY KEY,
    status      TEXT    NOT NULL DEFAULT 'open',
    priority    TEXT    NOT NULL DEFAULT 'normal',
    created_at  TEXT    NOT NULL,
    category_id INTEGER NOT NULL DEFAULT -1,
    subcat_id   INTEGER NOT NULL DEFAULT -1,
    text        TEXT    NOT NULL
);
CREATE TABLE IF NOT EXISTS categories (
    id   INTEGER PRIMARY KEY,
    name TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS subcategories (
    id          INTEGER PRIMARY KEY,
    category_id INTEGER NOT NULL,
    name        TEXT    NOT NULL
);
CREATE TABLE IF NOT EXISTS timesheet (
    id          INTEGER PRIMARY KEY,
    task_id     INTEGER NOT NULL,
    started_at  TEXT    NOT NULL,
    stopped_at  TEXT,
    notes       TEXT    NOT NULL DEFAULT ''
);
