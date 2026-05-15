import { FC } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { mui, YBButton, YBDropdown } from '@yugabyte-ui-library/core';
import { KeyboardArrowDown } from '@material-ui/icons';
import { getAddGeoPartitionRoute, getAddReadReplicaRoute } from '../../read-replica/readReplicaUtils';
import AddIcon from '@app/redesign/assets/add.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';

const { Box, MenuItem, Typography, Divider, Link } = mui;

interface PlacementActionsMenuProps {
  universeUuid?: string;
  /** When omitted, the "Edit master server node allocation" item is not shown (e.g. geo partition view). */
  onEditMasterAllocationClick?: () => void;
  /** When true, includes the "Add Geo Partition" item (used by the primary placement view). */
  showAddGeoPartition?: boolean;
  /** Optional override for the trigger button label key (defaults to "actions" common key). */
  triggerLabelKey?: string;
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
  showAddGeoPartition = false,
  triggerLabelKey
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.placement' });

  return (
    <YBDropdown
      sx={{ width: '340px' }}
      dataTestId="edit-placement-actions"
      slotProps={{
        paper: {
          sx: { width: '340px' }
        }
      }}
      origin={
        <YBButton
          variant="secondary"
          dataTestId="edit-placement-actions-button"
          endIcon={<KeyboardArrowDown />}
        >
          {triggerLabelKey ? t(triggerLabelKey, { keyPrefix: 'common' }) : t('actions')}
        </YBButton>
      }
    >
      {onEditMasterAllocationClick ? (
        <>
          <MenuItem
            data-test-id="edit-placement-clear-affinities"
            onClick={onEditMasterAllocationClick}
          >
            <Box sx={{ display: 'flex', alignItems: 'center', flexDirection: 'row', gap: '4px' }}>
              <EditIcon />
            </Box>
            {t('editMasterServerNodeAllocation')}
          </MenuItem>
          <Divider />
        </>
      ) : null}
      <MenuItem
        data-test-id="add-read-replica"
        sx={{ height: 'auto' }}
        onClick={() => {
          window.location.href = getAddReadReplicaRoute(universeUuid);
        }}
      >
        <Box sx={{ display: 'flex', alignItems: 'flex-start', flexDirection: 'row', gap: '4px' }}>
          <div>
            <AddIcon />
          </div>
          <Box sx={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
            {t('addReadReplica')}
            <Typography
              variant="subtitle1"
              color="textSecondary"
              sx={{ whiteSpace: 'initial' }}
            >
              <Trans t={t} i18nKey={'addReadReplicaHelpText'} style={{ lineHeight: '16px' }} />
            </Typography>
          </Box>
        </Box>
      </MenuItem>
      {showAddGeoPartition && (
        <>
          <Divider />
          <MenuItem
            data-test-id="add-geo-partition"
            sx={{ height: 'auto' }}
            onClick={() => {
              window.location.href = getAddGeoPartitionRoute(universeUuid);
            }}
          >
            <Box
              sx={{ display: 'flex', alignItems: 'flex-start', flexDirection: 'row', gap: '4px' }}
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
        </>
      )}
    </YBDropdown>
  );
};
