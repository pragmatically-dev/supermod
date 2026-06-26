-- Schema v3 — backlinks VIEW + l.metadata (audit #4).
--
-- Source-of-truth para la migración v2→v3. El string embebido vive en
-- Database.cpp (kMigrationV3); mantener ambos sincronizados.
--
-- Antes, BacklinksRepo::query hacía un 2º JOIN sobre `links` sólo para leer
-- l.metadata (source_page_key, usado por el backlink para volver a la página de
-- origen) porque el VIEW no lo exponía. Esto reconstruye el VIEW agregando
-- l.metadata, así la query lee b.metadata directo (un JOIN menos por fila).
--
-- Aplicado por migrateToV3() sólo si user_version < 3 (fresh installs van 0→1→2→3).

DROP VIEW IF EXISTS backlinks;
CREATE VIEW backlinks AS
SELECT
    l.id               AS link_id,
    l.source_doc_id,
    sd.name            AS source_doc_name,
    l.target_doc_id,
    l.target_page_key,
    td.name            AS target_doc_name,
    l.name             AS link_name,
    l.metadata,
    l.updated_at
FROM links l
JOIN docs sd ON sd.id = l.source_doc_id AND sd.deleted_at IS NULL
JOIN docs td ON td.id = l.target_doc_id AND td.deleted_at IS NULL
WHERE l.deleted_at IS NULL;

PRAGMA user_version = 3;
