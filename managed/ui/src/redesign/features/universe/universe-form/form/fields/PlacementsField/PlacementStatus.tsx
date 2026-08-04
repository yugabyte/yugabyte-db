import { ReactElement } from 'react';
import _ from 'lodash';
import { useTranslation } from 'react-i18next';
import { useWatch } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBHelper, YBHelperVariants } from '../../../../../../components';
import { Placement } from '../../../utils/dto';
import {
  REPLICATION_FACTOR_FIELD,
  PLACEMENTS_FIELD,
  MIN_PLACEMENTS_FOR_GEO_REDUNDANCY
} from '../../../utils/constants';

export const PlacementStatus = (): ReactElement | null => {
  const { t } = useTranslation();

  const STATUS_TYPES = {
    singleRF: {
      variant: YBHelperVariants.warning,
      msg: t('universeForm.cloudConfig.placementStatus.singleRF')
    },
    azWarning: {
      variant: YBHelperVariants.warning,
      msg: t('universeForm.cloudConfig.placementStatus.azWarning')
    },
    regionWarning: {
      variant: YBHelperVariants.success,
      msg: t('universeForm.cloudConfig.placementStatus.regionWarning')
    },
    multiRegion: {
      variant: YBHelperVariants.success,
      msg: t('universeForm.cloudConfig.placementStatus.multiRegion')
    },
    notEnoughNodesConfigured: {
      variant: YBHelperVariants.error,
      msg: t('universeForm.cloudConfig.placementStatus.notEnoughNodesConfigured')
    },
    notEnoughNodes: {
      variant: YBHelperVariants.error,
      msg: t('universeForm.cloudConfig.placementStatus.notEnoughNodes')
    },
    noFieldsChanged: {
      variant: YBHelperVariants.error,
      msg: t('universeForm.cloudConfig.placementStatus.noFieldsChanged')
    }
  };

  //watchers
  const replicationFactor = useWatch({ name: REPLICATION_FACTOR_FIELD });
  const currentPlacements = useWatch({ name: PLACEMENTS_FIELD });
  let currentStatusType;

  //check if AZ are placed in more than 2 regions
  const isMultiRegion =
    _.uniq(currentPlacements.map((e: Placement) => e.parentRegionId)).length >=
    MIN_PLACEMENTS_FOR_GEO_REDUNDANCY;
  //check if placements have more than 2 AZ
  const isMultiAz = currentPlacements.length >= MIN_PLACEMENTS_FOR_GEO_REDUNDANCY;

  if (replicationFactor === 1) currentStatusType = 'singleRF';
  else if (isMultiRegion) currentStatusType = 'multiRegion';
  else if (isMultiAz) currentStatusType = 'regionWarning';
  else currentStatusType = 'azWarning';

  if (currentPlacements?.length > 0)
    return (
      <Box width="100%" m={0.5}>
        <YBHelper variant={STATUS_TYPES[currentStatusType].variant}>
          {STATUS_TYPES[currentStatusType].msg}
        </YBHelper>
      </Box>
    );

  return null;
};
