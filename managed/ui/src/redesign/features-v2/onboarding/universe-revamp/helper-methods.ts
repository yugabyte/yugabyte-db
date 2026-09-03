import { useEffect, useState } from 'react';
import { RunTimeConfig } from '@app/redesign/features/universe/universe-form/utils/dto';
import {
  isNewUniverseExperienceForAllUsers,
  isV2CreateEditUniverseEnabled
} from '@app/redesign/features-v2/universe/create-universe/CreateUniverseUtils';
import { isRbacEnabled, isSuperAdminUser } from '@app/redesign/features/rbac/common/RbacUtils';
import { UserPermission } from '@app/redesign/features/rbac/common/rbac_constants';
import {
  ONBOARDING_NEW_EXPERIENCE_CHANGE_EVENT,
  isOnboardingNewExperienceEnabled,
  setOnboardingNewExperienceEnabled,
  syncOnboardingNewExperienceEnabled
} from './tour-progress';

export { ONBOARDING_NEW_EXPERIENCE_CHANGE_EVENT };
export {
  isOnboardingNewExperienceEnabled,
  setOnboardingNewExperienceEnabled,
  syncOnboardingNewExperienceEnabled
};

export const ONBOARDING_FULLSCREEN_OVERLAY_EVENT = 'yb-onboarding-fullscreen-overlay-change';
export const EDIT_PLACEMENT_OVERLAY_ID = 'edit-placement';
export const UNIVERSE_FORM_OVERLAY_ID = 'universe-form';

const openFullscreenOverlayIds = new Set<string>();

const emitFullscreenOverlayChange = (): void => {
  const open = openFullscreenOverlayIds.size > 0;
  document.body.classList.toggle('onboarding-fullscreen-overlay-open', open);
  window.dispatchEvent(
    new CustomEvent(ONBOARDING_FULLSCREEN_OVERLAY_EVENT, { detail: { open } })
  );
};

/** Hide the onboarding banner while a fullscreen flow (Edit Placement / Universe Form) is open. */
export const setOnboardingFullscreenOverlayOpen = (id: string, open: boolean): void => {
  if (open) {
    openFullscreenOverlayIds.add(id);
  } else {
    openFullscreenOverlayIds.delete(id);
  }
  emitFullscreenOverlayChange();
};

export const isOnboardingFullscreenOverlayOpen = (): boolean => openFullscreenOverlayIds.size > 0;

export const subscribeOnboardingNewExperienceChange = (
  onChange: (enabled: boolean) => void
): (() => void) => {
  const handler = (event: Event) => {
    onChange(Boolean((event as CustomEvent<{ enabled: boolean }>).detail?.enabled));
  };
  window.addEventListener(ONBOARDING_NEW_EXPERIENCE_CHANGE_EVENT, handler);
  return () => window.removeEventListener(ONBOARDING_NEW_EXPERIENCE_CHANGE_EVENT, handler);
};

/** React helper for SuperAdmin opt-in toggle state. */
export const useOnboardingNewExperienceEnabled = (): boolean => {
  const [enabled, setEnabled] = useState(isOnboardingNewExperienceEnabled);
  useEffect(() => subscribeOnboardingNewExperienceChange(setEnabled), []);
  return enabled;
};

export const useOnboardingFullscreenOverlayOpen = (): boolean => {
  const [open, setOpen] = useState(isOnboardingFullscreenOverlayOpen);
  useEffect(() => {
    const handler = (event: Event) => {
      setOpen(Boolean((event as CustomEvent<{ open: boolean }>).detail?.open));
    };
    window.addEventListener(ONBOARDING_FULLSCREEN_OVERLAY_EVENT, handler);
    return () => window.removeEventListener(ONBOARDING_FULLSCREEN_OVERLAY_EVENT, handler);
  }, []);
  return open;
};

export const isCurrentUserSuperAdmin = (currentUserRole?: string): boolean => {
  const rbacPermissions = (window as unknown as { rbac_permissions?: UserPermission[] })
    .rbac_permissions;
  return isRbacEnabled()
    ? isSuperAdminUser(rbacPermissions ?? [])
    : currentUserRole === 'SuperAdmin';
};

/**
 * True when enable_new_universe_experience is on and either
 * enable_new_universe_experience_for_all_users is on or the user is SuperAdmin.
 *
 * SuperAdmin uses the in-memory mirror (hydrated from runtime config, updated by
 * the banner toggle) so the UI flips immediately without waiting for refetch.
 */
export const isUniverseRevampExperienceEnabled = (
  runtimeConfigs?: RunTimeConfig,
  currentUserRole?: string
): boolean => {
  if (isCurrentUserSuperAdmin(currentUserRole)) {
    return isOnboardingNewExperienceEnabled();
  }
  return (
    isV2CreateEditUniverseEnabled(runtimeConfigs as RunTimeConfig) &&
    isNewUniverseExperienceForAllUsers(runtimeConfigs as RunTimeConfig)
  );
};
