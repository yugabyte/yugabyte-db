export const UpgradeStageCategory = {
  UPGRADE: 'upgrade',
  FINALIZE: 'finalize'
} as const;
export type UpgradeStageCategory =
  (typeof UpgradeStageCategory)[keyof typeof UpgradeStageCategory];
