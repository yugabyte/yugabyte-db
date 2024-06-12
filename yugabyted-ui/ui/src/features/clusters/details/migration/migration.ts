/**
 * Migration Phases (Statuses from Voyager)
 */
export enum MigrationPhase {
  "Export Schema" = 0,
  "Analyze Schema" = 1,
  "Export Data" = 2,
  "Import Schema" = 3,
  "Import Data" = 4,
  "Verify" = 5,
}
export const migrationPhases = Object.keys(MigrationPhase)
  .map((key) => MigrationPhase[key as any])
  .filter((value) => typeof value === "string");

/**
 * Migration Steps (Cards shown in the UI)
 */
export enum MigrationStep {
  "Assessment" = 0,
  "Schema Migration" = 1,
  "Data Migration" = 2,
  "Verification" = 3,
}
export const migrationSteps = Object.keys(MigrationStep)
  .map((key) => MigrationStep[key as any])
  .filter((value) => typeof value === "string");
