import { FC } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { mui, YBButton, YBDropdown } from '@yugabyte-ui-library/core';
import { KeyboardArrowDown } from '@material-ui/icons';
import {
  getAddGeoPartitionRoute,
  getAddReadReplicaRoute
} from '../../read-replica/readReplicaUtils';
import AddIcon from '@app/redesign/assets/add.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { useIsUniverseReady, withUniverseResource } from '../EditUniverseUtils';
import {
  AdvancedPlacementPopover,
  useAdvancedPlacementPopover
} from '@app/redesign/features-v2/onboarding/universe-revamp/popovers/AdvancedPlacementPopover';

const DEDICATED_NODES_LINK =
  'https://docs.yugabyte.com/stable/yugabyte-platform/create-deployments/dedicated-master';
const ADD_READ_REPLICA_LINK =
  'https://docs.yugabyte.com/stable/yugabyte-platform/create-deployments/read-replicas';

const { Box, MenuItem, Typography, Divider, Link } = mui;

interface PlacementActionsMenuProps {
  universeUuid?: string;
  /** When omitted, the master server node allocation item is not shown (e.g. geo partition view). */
  onEditMasterAllocationClick?: () => void;
  /** When true, masters are on dedicated nodes — shows "Edit …" without help text. */
  useDedicatedNodes?: boolean;
  /** When true, includes the "Add Geo Partition" item (used by the primary placement view). */
  showAddGeoPartition?: boolean;
  /** Optional override for the trigger button label key (defaults to "actions" common key). */
  triggerLabelKey?: string;
  /** When true, the "Add Read Replica" item is not shown. */
  readReplicaAlreadyPresent?: boolean;
}

/**
 * Shared action menu rendered above placement cards in both the primary
 * `PlacementTab` and the `GeoPartitionPlacementView`. Behavior is identical
 * across both call sites; the only variation is whether the third item
 * ("Add Geo Partition") is visible.
 */
export const PlacementActionsMenu: FC<PlacementActionsMenuProps> = ({
  universeUuid,
  onEditMasterAllocationClick,
  useDedicatedNodes = false,
  showAddGeoPartition = false,
  triggerLabelKey,
  readReplicaAlreadyPresent = false
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.placement' });
  const isUniverseReady = useIsUniverseReady();
  const showAddReadReplica = !readReplicaAlreadyPresent;
  const hasMenuItems =
    !!onEditMasterAllocationClick || showAddReadReplica || showAddGeoPartition;
  // Only show tip for the default "Advanced Placement options" trigger (Placement tab).
  const showAdvancedPlacementTip = !triggerLabelKey && hasMenuItems;
  const {
    open: isAdvancedPlacementPopoverOpen,
    anchorRef: advancedPlacementAnchorRef,
    handleClose: handleAdvancedPlacementPopoverClose,
    handleClickAway: handleAdvancedPlacementPopoverClickAway
  } = useAdvancedPlacementPopover(showAdvancedPlacementTip);

  if (!hasMenuItems) {
    return null;
  }

  const triggerButton = (
    <YBButton
      variant="ghost"
      dataTestId="edit-placement-actions-button"
      endIcon={<KeyboardArrowDown />}
    >
      {triggerLabelKey
        ? t(triggerLabelKey, { keyPrefix: 'common' })
        : t('advancedPlacementOptions')}
    </YBButton>
  );

  // if it is k8's and read replica is already present, then we remove the actions menu as there are no actions to show
  if(!showAddReadReplica && !onEditMasterAllocationClick) {
    return null;
  }
  return (
    <>
      <span
        ref={showAdvancedPlacementTip ? advancedPlacementAnchorRef : undefined}
        style={{ display: 'inline-block' }}
      >
        <YBDropdown
          dataTestId="edit-placement-actions"
          disableScrollLock
          slotProps={{
            paper: {
              sx: { width: '340px' }
            }
          }}
          origin={triggerButton}
        >
          {onEditMasterAllocationClick ? (
            <>
              <RbacValidator accessRequiredOn={withUniverseResource(ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER, universeUuid)} isControl>
                {useDedicatedNodes ? (
                  <MenuItem
                    data-test-id="edit-placement-clear-affinities"
                    onClick={onEditMasterAllocationClick}
                    disabled={!isUniverseReady}
                  >
                    <Box
                      sx={{
                        display: 'flex',
                        alignItems: 'center',
                        flexDirection: 'row',
                        gap: '4px'
                      }}
                    >
                      <EditIcon />
                      {t('editMasterServerNodeAllocation')}
                    </Box>
                  </MenuItem>
                ) : (
                  <MenuItem
                    data-test-id="edit-placement-clear-affinities"
                    sx={{ height: 'auto' }}
                    onClick={onEditMasterAllocationClick}
                    disabled={!isUniverseReady}
                  >
                    <Box
                      sx={{
                        display: 'flex',
                        alignItems: 'flex-start',
                        flexDirection: 'row',
                        gap: '4px'
                      }}
                    >
                      <div>
                        <AddIcon />
                      </div>
                      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
                        {t('masterServerNodeAllocation')}
                        <Typography
                          variant="subtitle1"
                          color="textSecondary"
                          sx={{ whiteSpace: 'initial' }}
                        >
                          <Trans
                            t={t}
                            i18nKey="masterServerNodeAllocationHelpText"
                            components={{
                              a: (
                                <Link
                                  href={DEDICATED_NODES_LINK}
                                  target="_blank"
                                  rel="noopener noreferrer"
                                  onClick={(e) => e.stopPropagation()}
                                />
                              )
                            }}
                            style={{ lineHeight: '16px' }}
                          />
                        </Typography>
                      </Box>
                    </Box>
                  </MenuItem>
                )}
              </RbacValidator>
            </>
          ) : null}
          {showAddReadReplica ? (
            <>
              {onEditMasterAllocationClick ? <Divider /> : null}
              <RbacValidator accessRequiredOn={withUniverseResource(ApiPermissionMap.ADD_V2_READ_REPLICA, universeUuid)} isControl>
                <MenuItem
                  data-test-id="add-read-replica"
                  sx={{ height: 'auto' }}
                  onClick={() => {
                    window.location.href = getAddReadReplicaRoute(universeUuid);
                  }}
                  disabled={!isUniverseReady}
                >
                  <Box
                    sx={{
                      display: 'flex',
                      alignItems: 'flex-start',
                      flexDirection: 'row',
                      gap: '4px'
                    }}
                  >
                    <div>
                      <AddIcon />
                    </div>
                    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
                      {t('addReadReplica')}
                      <Typography
                        variant="subtitle1"
                        color="textSecondary"
                        sx={{ whiteSpace: 'initial' }}
                      >
                        <Trans
                          t={t}
                          i18nKey="addReadReplicaHelpText"
                          components={{
                            a: (
                              <Link
                                href={ADD_READ_REPLICA_LINK}
                                target="_blank"
                                rel="noopener noreferrer"
                                onClick={(e) => e.stopPropagation()}
                              />
                            )
                          }}
                          style={{ lineHeight: '16px' }}
                        />
                      </Typography>
                    </Box>
                  </Box>
                </MenuItem>
              </RbacValidator>
            </>
          ) : null}
          {showAddGeoPartition && (
            <>
              {(onEditMasterAllocationClick !== undefined || showAddReadReplica) && <Divider />}
              <RbacValidator accessRequiredOn={withUniverseResource(ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER, universeUuid)} isControl>
                <MenuItem
                  data-test-id="add-geo-partition"
                  sx={{ height: 'auto' }}
                  onClick={() => {
                    window.location.href = getAddGeoPartitionRoute(universeUuid);
                  }}
                  disabled={!isUniverseReady}
                >
                  <Box
                    sx={{
                      display: 'flex',
                      alignItems: 'flex-start',
                      flexDirection: 'row',
                      gap: '4px'
                    }}
                  >
                    <div>
                      <AddIcon />
                    </div>
                    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
                      {t('addGeoPartition')}
                      <Typography
                        variant="subtitle1"
                        color="textSecondary"
                        sx={{ whiteSpace: 'initial' }}
                      >
                        <Trans
                          t={t}
                          i18nKey={'geoPartitionHelpText'}
                          components={{ a: <Link /> }}
                          style={{ lineHeight: '16px' }}
                        />
                      </Typography>
                    </Box>
                  </Box>
                </MenuItem>
              </RbacValidator>
            </>
          )}
        </YBDropdown>
      </span>
      {showAdvancedPlacementTip && (
        <AdvancedPlacementPopover
          open={isAdvancedPlacementPopoverOpen}
          anchorRef={advancedPlacementAnchorRef}
          onClose={handleAdvancedPlacementPopoverClose}
          onClickAway={handleAdvancedPlacementPopoverClickAway}
        />
      )}
    </>
  );
};
