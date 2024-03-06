import { XClusterTableEligibility } from '../../constants';
import { MainTableReplicationCandidate } from '../../XClusterTypes';

export const shouldShowTableEligibilityPill = (xClusterTable: MainTableReplicationCandidate) =>
  xClusterTable.eligibilityDetails.status !== XClusterTableEligibility.ELIGIBLE_UNUSED;
