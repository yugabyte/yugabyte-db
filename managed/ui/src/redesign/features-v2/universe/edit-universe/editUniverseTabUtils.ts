import { EditUniverseTabs } from './EditUniverseContext';

const EDIT_UNIVERSE_TAB_VALUES = new Set<string>(Object.values(EditUniverseTabs));

export function parseEditUniverseTabFromPath(tab?: string): EditUniverseTabs {
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
  tab: EditUniverseTabs = EditUniverseTabs.GENERAL
): string {
  return `/universes/${universeUuid}/settings/${tab}`;
}
