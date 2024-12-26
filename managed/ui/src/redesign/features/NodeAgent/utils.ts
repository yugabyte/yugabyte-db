import { parseDurationToSeconds } from '../../../utils/parsers';

/**
 * Node agent is enabled when the enabler scan interval is greater than 0 seconds.
 */
export const getIsNodeAgentEnabled = (nodeAgentEnablerScanInterval: string) =>
  parseDurationToSeconds(nodeAgentEnablerScanInterval, { noThrow: true }) > 0;
