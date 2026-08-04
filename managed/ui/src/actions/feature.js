export const TOGGLE_FEATURE = 'features/TOGGLE_FEATURE';

export function toggleFeature(status, feature) {
  return {
    type: TOGGLE_FEATURE,
    feature
  };
}
