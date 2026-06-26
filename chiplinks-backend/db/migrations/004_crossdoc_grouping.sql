-- 004_crossdoc_grouping.sql — schema v4 (LINKS_REDESIGN.md step 1)
-- Additive cross-doc grouping support + backlinks VIEW rebuild. Mirror of kMigrationV4
-- in db/Database.cpp. Applied inside BEGIN IMMEDIATE by Database::migrateToV4().
-- Forward-only; a downgraded .so keeps the extra columns/VIEW harmlessly.

-- (a) links: birth page in the source doc (the backlink anchor) + write-time human
--     label of the target page.
ALTER TABLE links ADD COLUMN source_page INTEGER;
ALTER TABLE links ADD COLUMN target_page_label TEXT NOT NULL DEFAULT '';

-- Backfill source_page from the existing pin (best-effort; pinless legacy rows stay
-- NULL -> rendered as "Go to source").
UPDATE links SET source_page = (
    SELECT p.source_page FROM pins p
    WHERE p.link_id = links.id
    ORDER BY p.source_page LIMIT 1
) WHERE source_page IS NULL;

-- Normalize doc-level keys to '' going forward (data fix only, NO check constraint).
UPDATE links SET target_page_key = '' WHERE target_page_key IS NULL;

-- (b) recents: stored target-page label so the panel shows the page, not a bare name.
ALTER TABLE recents ADD COLUMN target_page_label TEXT NOT NULL DEFAULT '';

-- (c) backlinks VIEW rebuild: ADD source_page (read from l.source_page, no LEFT JOIN
--     pins) + filter self-links in SQL. target_doc_name is KEPT for now so the still-live
--     BacklinksRepo::query keeps working; it is dropped in step 4 with the grouped read.
DROP VIEW IF EXISTS backlinks;
CREATE VIEW backlinks AS
SELECT
    l.id               AS link_id,
    l.source_doc_id,
    sd.name            AS source_doc_name,
    l.source_page,
    l.target_doc_id,
    l.target_page_key,
    td.name            AS target_doc_name,
    l.name             AS link_name,
    l.metadata,
    l.updated_at
FROM links l
JOIN docs sd ON sd.id = l.source_doc_id AND sd.deleted_at IS NULL
JOIN docs td ON td.id = l.target_doc_id AND td.deleted_at IS NULL
WHERE l.deleted_at IS NULL
  AND l.source_doc_id <> l.target_doc_id;

PRAGMA user_version = 4;
