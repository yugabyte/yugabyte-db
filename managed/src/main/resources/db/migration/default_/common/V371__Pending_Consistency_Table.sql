CREATE TABLE IF NOT EXISTS pending_consistency_check (
  universe_uuid   UUID NOT NULL,
  task_uuid       UUID NOT NULL,
  pending         BOOLEAN NOT NULL,
  CONSTRAINT fk_pend_universe_uuid FOREIGN KEY (universe_uuid) REFERENCES universe(universe_uuid),
  CONSTRAINT pk_pend_task_uuid PRIMARY KEY (task_uuid)
)