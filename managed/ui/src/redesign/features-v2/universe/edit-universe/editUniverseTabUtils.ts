import { objToQueryParams } from '@app/utils/ObjectUtils';
import { EditUniverseTabs } from './EditUniverseContext';

const EDIT_UNIVERSE_TAB_VALUES = new Set<string>(Object.values(EditUniverseTabs));

export function parseEditUniverseTabFromQuery(tab?: string): EditUniverseTabs {
  if (tab && EDIT_UNIVERSE_TAB_VALUES.has(tab)) {
    return tab as EditUniverseTabs;
  }
  return EditUniverseTabs.GENERAL;
}

export function isValidEditUniverseTab(tab?: string): tab is EditUniverseTabs {
  return !!tab && EDIT_UNIVERSE_TAB_VALUES.has(tab);
}

export function getEditUniverseSettingsRoute(
  universeUuid: string,
  tab?: EditUniverseTabs
): string {
  const base = `/universes/${universeUuid}/settings`;
  if (!tab) {
    return base;
  }
  return `${base}?${objToQueryParams({ tab })}`;
}
