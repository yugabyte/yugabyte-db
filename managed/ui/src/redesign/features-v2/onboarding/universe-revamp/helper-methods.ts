import { useEffect, useState } from 'react';
import { RunTimeConfig } from '@app/redesign/features/universe/universe-form/utils/dto';
import { isV2CreateEditUniverseEnabled } from '@app/redesign/features-v2/universe/create-universe/CreateUniverseUtils';
import { isRbacEnabled, isSuperAdminUser } from '@app/redesign/features/rbac/common/RbacUtils';
import { UserPermission } from '@app/redesign/features/rbac/common/rbac_constants';

export const ONBOARDING_NEW_EXPERIENCE_KEY = 'yb_onboarding_new_experience_enabled';
export const ONBOARDING_NEW_EXPERIENCE_CHANGE_EVENT = 'yb-onboarding-new-experience-change';
export const ONBOARDING_BANNER_DISMISS_KEY = 'yb_onboarding_banner_dismissed';

export const ONBOARDING_FULLSCREEN_OVERLAY_EVENT = 'yb-onboarding-fullscreen-overlay-change';
export const EDIT_PLACEMENT_OVERLAY_ID = 'edit-placement';
export const UNIVERSE_FORM_OVERLAY_ID = 'universe-form';

/** All onboarding localStorage keys — cleared on logout. Keep in sync with tip/modal modules. */
const UNIVERSE_REVAMP_ONBOARDING_STORAGE_KEYS = [
  ONBOARDING_NEW_EXPERIENCE_KEY,
  ONBOARDING_BANNER_DISMISS_KEY,
  'yb_before_new_experience_popover_dismissed',
  'yb_after_new_experience_popover_dismissed',
  'yb_universe_creation_popover_dismissed',
  'yb_detail_settings_popover_dismissed',
  'yb_advanced_placement_popover_dismissed',
  'yb_guided_expert_mode_popover_dismissed',
  'yb_before_proceed_with_new_modal_dismissed',
  'yb_what_changed_modal_dismissed',
  'yb_what_new_in_placement_modal_dismissed',
  'yb_where_things_moved_modal_dismissed'
];

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

/** Clears onboarding localStorage keys. Call on logout. */
export const resetUniverseRevampOnboardingStorage = (): void => {
  UNIVERSE_REVAMP_ONBOARDING_STORAGE_KEYS.forEach((key) => {
    localStorage.removeItem(key);
  });
};

export const isOnboardingNewExperienceEnabled = (): boolean =>
  localStorage.getItem(ONBOARDING_NEW_EXPERIENCE_KEY) === 'true';

export const setOnboardingNewExperienceEnabled = (enabled: boolean): void => {
  localStorage.setItem(ONBOARDING_NEW_EXPERIENCE_KEY, String(enabled));
  window.dispatchEvent(
    new CustomEvent(ONBOARDING_NEW_EXPERIENCE_CHANGE_EVENT, { detail: { enabled } })
  );
};

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
 * True when V2 runtime is on, or SuperAdmin has opted in via localStorage.
 * Pass `onboardingOptInEnabled` from `useOnboardingNewExperienceEnabled()` so UI
 * updates when the banner toggle changes.
 */
export const isUniverseRevampExperienceEnabled = (
  runtimeConfigs?: RunTimeConfig,
  currentUserRole?: string,
  onboardingOptInEnabled: boolean = isOnboardingNewExperienceEnabled()
): boolean =>
  isV2CreateEditUniverseEnabled(runtimeConfigs as RunTimeConfig) ||
  (isCurrentUserSuperAdmin(currentUserRole) && onboardingOptInEnabled);
