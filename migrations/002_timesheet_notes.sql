-- Adds notes column to timesheet tables created before migration 001.
-- The runner ignores "duplicate column name" errors so this is safe to
-- apply against databases where the column already exists.
ALTER TABLE timesheet ADD COLUMN notes TEXT NOT NULL DEFAULT '';
