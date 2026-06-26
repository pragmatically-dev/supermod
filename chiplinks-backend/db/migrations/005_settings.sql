-- Schema v5 — SuperMod global settings (one persisted, sectioned key/value store).
--
-- Source-of-truth para la migración v4→v5. El string embebido vive en
-- Database.cpp (kMigrationV5); mantener ambos sincronizados.
--
-- A diferencia de `meta` (KV plano para app-state suelto), `settings` es el store
-- de configuración GLOBAL del usuario por mod: cada fila es un parámetro tunable
-- (dimensiones de pin/post-it, lienzo del editor, params de graphview, esquina del
-- FAB, …). El DEFAULT/min/max/step/tipo de cada key NO vive acá sino en C++
-- (SettingsRepo::schema) — única fuente de verdad; la tabla sólo guarda overrides
-- del usuario. Una key ausente ⇒ se usa el default del schema. Global (sin doc_id).
--
-- value es TEXT: los números/enum se serializan como string y el schema los tipa
-- (igual patrón que meta). section agrupa para la UI (chiplinks/postit/graph).

CREATE TABLE settings (
    key        TEXT PRIMARY KEY,
    section    TEXT NOT NULL,
    value      TEXT NOT NULL,
    updated_at INTEGER NOT NULL DEFAULT (unixepoch())
);
CREATE INDEX idx_settings_section ON settings(section);

CREATE TRIGGER trg_settings_updated AFTER UPDATE ON settings
WHEN OLD.updated_at = NEW.updated_at
BEGIN
    UPDATE settings SET updated_at = unixepoch() WHERE key = NEW.key;
END;

PRAGMA user_version = 5;
