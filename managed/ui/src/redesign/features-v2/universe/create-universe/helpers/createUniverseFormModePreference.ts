import axios from 'axios';
import { ROOT_URL } from '@app/config';
import { ResilienceFormMode } from '../steps/resilence-regions/dtos';

/** Per-user sticky default for revamp create-universe Guided/Expert. */
export const CREATE_UNIVERSE_FORM_MODE_SETTINGS_KEY = 'create_universe_form_mode';

export function resolveCreateUniverseFormMode(
  settings?: Record<string, unknown> | null
): ResilienceFormMode {
  const raw = settings?.[CREATE_UNIVERSE_FORM_MODE_SETTINGS_KEY];
  if (raw === ResilienceFormMode.GUIDED || raw === ResilienceFormMode.EXPERT_MODE) {
    return raw;
  }
  return ResilienceFormMode.GUIDED;
}

/** Fire-and-forget profile write; never throws (create must not fail on this). */
export async function persistCreateUniverseFormMode(
  user: { uuid?: string; role?: string } | null | undefined,
  mode: ResilienceFormMode
): Promise<void> {
  if (!user?.uuid || !user.role) return;
  try {
    const cUUID = localStorage.getItem('customerId');
    await axios.put(`${ROOT_URL}/customers/${cUUID}/users/${user.uuid}/update_profile`, {
      role: user.role,
      userSettings: { [CREATE_UNIVERSE_FORM_MODE_SETTINGS_KEY]: mode }
    });
  } catch {
    // swallow
  }
}
