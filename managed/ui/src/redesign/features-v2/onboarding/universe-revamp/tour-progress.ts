import axios from 'axios';
import { ROOT_URL } from '@app/config';

export const TOUR_SETTINGS_KEY = 'universe_revamp_popups';
export const ONBOARDING_NEW_EXPERIENCE_CHANGE_EVENT = 'yb-onboarding-new-experience-change';
export const TOUR_PROGRESS_READY_EVENT = 'yb-tour-progress-ready';

/** Append-only bit indices — never reorder or reuse. */
export const TourStep = {
  Banner: 0,
  BeforeExp: 1,
  AfterExp: 2,
  UnivCreate: 3,
  DetailSettings: 4,
  AdvPlacement: 5,
  GuidedExpert: 6,
  BeforeProceed: 7,
  WhatChanged: 8,
  WhatNewPlacement: 9,
  WhereMoved: 10
} as const;

export type TourStepBit = typeof TourStep[keyof typeof TourStep];

/** Permanent tour steps used for newUniverseUiTourCompleted (excludes reopenable tips). */
const ALL_MASK = ((1 << TourStep.WhatChanged) - 1) | (1 << TourStep.WhatNewPlacement);

type UserRef = { uuid: string; role: string };

let mask = 0;
let newUiEnabled = false;
let userRef: UserRef | null = null;
let onUserUpdate: ((user: unknown) => void) | null = null;
let persistChain: Promise<void> = Promise.resolve();

const bit = (step: TourStepBit) => 1 << step;

export const isTourProgressReady = (): boolean => userRef !== null;

export function subscribeTourProgressReady(onReady: () => void): () => void {
  if (isTourProgressReady()) {
    onReady();
    return () => undefined;
  }
  const handler = () => onReady();
  window.addEventListener(TOUR_PROGRESS_READY_EVENT, handler);
  return () => window.removeEventListener(TOUR_PROGRESS_READY_EVENT, handler);
}

export const isTourStepDismissed = (step: TourStepBit): boolean => (mask & bit(step)) !== 0;

export const isOnboardingNewExperienceEnabled = (): boolean => newUiEnabled;

export function bindTourProgress(
  user: {
    uuid?: string;
    role?: string;
    settings?: Record<string, unknown> | null;
    newUniverseUiEnabled?: boolean;
    newUniverseUiTourCompleted?: boolean;
  } | null,
  onUpdate?: (user: unknown) => void
): void {
  if (onUpdate) onUserUpdate = onUpdate;
  if (!user?.uuid || !user.role) {
    userRef = null;
    return;
  }
  const wasReady = userRef !== null;
  userRef = { uuid: user.uuid, role: user.role };
  const enabled = !!user.newUniverseUiEnabled;
  const prev = newUiEnabled;
  newUiEnabled = enabled;
  const raw = user.settings?.[TOUR_SETTINGS_KEY];
  mask = typeof raw === 'number' ? raw : 0;
  if (enabled !== prev) {
    window.dispatchEvent(
      new CustomEvent(ONBOARDING_NEW_EXPERIENCE_CHANGE_EVENT, { detail: { enabled } })
    );
  }
  if (!wasReady) {
    window.dispatchEvent(new CustomEvent(TOUR_PROGRESS_READY_EVENT));
  }
}

/** Call on logout so the next session does not reuse in-memory progress. */
export function resetTourProgress(): void {
  mask = 0;
  newUiEnabled = false;
  userRef = null;
}

function persist(): void {
  persistChain = persistChain
    .then(async () => {
      if (!userRef) return;
      const body: Record<string, unknown> = {
        role: userRef.role,
        userSettings: { [TOUR_SETTINGS_KEY]: mask }
      };
      if ((mask & ALL_MASK) === ALL_MASK) {
        body.newUniverseUiTourCompleted = true;
      }
      const cUUID = localStorage.getItem('customerId');
      const { data } = await axios.put(
        `${ROOT_URL}/customers/${cUUID}/users/${userRef.uuid}/update_profile`,
        body
      );
      onUserUpdate?.(data);
    })
    .catch(() => undefined);
}

export function dismissTourStep(step: TourStepBit): void {
  mask |= bit(step);
  persist();
}

export function setOnboardingNewExperienceEnabled(enabled: boolean): void {
  if (newUiEnabled === enabled) return;
  newUiEnabled = enabled;
  window.dispatchEvent(
    new CustomEvent(ONBOARDING_NEW_EXPERIENCE_CHANGE_EVENT, { detail: { enabled } })
  );
  persistChain = persistChain
    .then(async () => {
      if (!userRef) return;
      const cUUID = localStorage.getItem('customerId');
      const { data } = await axios.put(
        `${ROOT_URL}/customers/${cUUID}/users/${userRef.uuid}/update_profile`,
        { role: userRef.role, newUniverseUiEnabled: enabled }
      );
      onUserUpdate?.(data);
    })
    .catch(() => undefined);
}
