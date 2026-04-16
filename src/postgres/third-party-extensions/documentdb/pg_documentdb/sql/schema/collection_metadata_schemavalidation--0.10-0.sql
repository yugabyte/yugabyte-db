
ALTER TABLE __API_CATALOG_SCHEMA__.collections 
        ADD COLUMN validator __CORE_SCHEMA__.bson DEFAULT null,
        ADD COLUMN validation_level text DEFAULT null CONSTRAINT validation_level_check CHECK (validation_level IN ('off', 'strict', 'moderate')),
        ADD COLUMN validation_action text DEFAULT null CONSTRAINT validation_action_check CHECK (validation_action IN ('warn', 'error'));