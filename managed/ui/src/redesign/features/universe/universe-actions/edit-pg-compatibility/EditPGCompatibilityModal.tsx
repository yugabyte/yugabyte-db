import { FC } from 'react';
import { get, cloneDeep } from 'lodash';
import { toast } from 'react-toastify';
import { useForm } from 'react-hook-form';
import { useMutation } from 'react-query';
import { useTranslation, Trans } from 'react-i18next';
import { Box, makeStyles, Typography, Link } from '@material-ui/core';
import { YBModal, YBToggleField } from '../../../../components';
import {
  getPrimaryCluster,
  createErrorMessage,
  getCurrentVersion,
  getAsyncCluster
} from '../../universe-form/utils/helpers';
import { api } from '../../universe-form/utils/api';
import { EditGflagPayload, UpgradeOptions } from '../edit-gflags/GflagHelper';
import { isPGEnabledFromIntent } from '../../universe-form/utils/helpers';
import { RBAC_ERR_MSG_NO_PERM } from '../../../rbac/common/validator/ValidatorUtils';
import { hasNecessaryPerm } from '../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../rbac/ApiAndUserPermMapping';
import { Universe } from '../../universe-form/utils/dto';
import { DEFAULT_SLEEP_INTERVAL_IN_MS } from '../../universe-form/utils/constants';
import { GFLAG_GROUPS } from '../../../../helpers/constants';

//icons
import InfoBlue from '../../../../assets/info-blue.svg';
import InfoError from '../../../../assets/info-red.svg';

interface PGCompatibilityModalProps {
  open: boolean;
  onClose: () => void;
  universeData: Universe;
}

const useStyles = makeStyles((theme) => ({
  toggleContainer: {
    display: 'flex',
    flexDirection: 'column',
    height: 88,
    width: '100%',
    rowGap: 8,
    backgroundColor: '#FCFCFC',
    border: '1px solid #E5E5E9',
    borderRadius: 8,
    padding: theme.spacing(2)
  },
  toggleRow: {
    display: 'flex',
    flexDirection: 'row',
    width: '100%',
    justifyContent: 'space-between',
    alignItems: 'flex-start'
  },
  subText: {
    fontSize: 12,
    fontWeight: 400,
    color: '#67666C',
    marginTop: 4,
    lineHeight: '18px'
  },
  infoContainer: {
    display: 'flex',
    flexDirection: 'row',
    height: 'auto',
    width: '100%',
    padding: theme.spacing(1),
    backgroundColor: '#D7EFF4',
    borderRadius: 8,
    alignItems: 'flex-start',
    columnGap: '8px'
  },
  errorContainer: {
    display: 'flex',
    flexDirection: 'row',
    height: 'auto',
    width: '100%',
    padding: theme.spacing(1),
    backgroundColor: '#FDE2E2',
    borderRadius: 8,
    alignItems: 'flex-start',
    columnGap: '8px'
  },
  learnLink: {
    color: 'inherit',
    marginLeft: 24
  },
  uList: {
    listStyleType: 'disc',
    paddingLeft: '24px',
    marginBottom: 0
  }
}));

type PGFormValues = {
  enablePGCompatibitilty: boolean;
};

export const EditPGCompatibilityModal: FC<PGCompatibilityModalProps> = ({
  open,
  onClose,
  universeData
}) => {
  const { t } = useTranslation();
  const classes = useStyles();
  const { universeDetails, universeUUID } = universeData;
  const { nodePrefix } = universeDetails;
  let primaryCluster = cloneDeep(getPrimaryCluster(universeDetails));
  const universeName = get(primaryCluster, 'userIntent.universeName');
  const currentRF = get(primaryCluster, 'userIntent.replicationFactor');
  const isPGEnabled = primaryCluster ? isPGEnabledFromIntent(primaryCluster?.userIntent) : false;

  const { control, handleSubmit, watch } = useForm<PGFormValues>({
    defaultValues: {
      enablePGCompatibitilty: isPGEnabled ? true : false
    }
  });

  const upgradePGCompatibility = useMutation(
    (values: EditGflagPayload) => {
      return api.upgradeGflags(values, universeUUID);
    },
    {
      onSuccess: () => {
        onClose();
      },
      onError: (error) => {
        toast.error(createErrorMessage(error));
      }
    }
  );

  //watchers
  const pgToggleValue = watch('enablePGCompatibitilty');

  const handleFormSubmit = handleSubmit(async (formValues) => {
    try {
      const payload: EditGflagPayload = {
        nodePrefix,
        universeUUID,
        sleepAfterMasterRestartMillis: get(
          universeDetails,
          'sleepAfterMasterRestartMillis',
          DEFAULT_SLEEP_INTERVAL_IN_MS
        ),
        sleepAfterTServerRestartMillis: get(
          universeDetails,
          'sleepAfterTServerRestartMillis',
          DEFAULT_SLEEP_INTERVAL_IN_MS
        ),
        taskType: 'GFlags',
        upgradeOption: UpgradeOptions.Rolling,
        ybSoftwareVersion: getCurrentVersion(universeDetails) || '',
        clusters: []
      };
      if (primaryCluster && primaryCluster.userIntent?.specificGFlags) {
        primaryCluster.userIntent.specificGFlags = {
          ...primaryCluster.userIntent.specificGFlags,
          gflagGroups: formValues.enablePGCompatibitilty
            ? [GFLAG_GROUPS.ENHANCED_POSTGRES_COMPATIBILITY]
            : []
        };
        payload.clusters.push(primaryCluster);
      }
      if (universeDetails.clusters.length > 1) {
        const asyncCluster = getAsyncCluster(universeDetails);
        if (asyncCluster) payload.clusters.push(asyncCluster);
      }
      await upgradePGCompatibility.mutateAsync(payload);
    } catch (e) {
      console.error(createErrorMessage(e));
    }
  });

  const canEditGFlags = hasNecessaryPerm({
    onResource: universeUUID,
    ...ApiPermissionMap.UPGRADE_UNIVERSE_GFLAGS
  });

  return (
    <YBModal
      open={open}
      title={t('universeActions.pgCompatibility.modalTitle')}
      submitLabel={t('common.applyChanges')}
      cancelLabel={t('common.cancel')}
      onClose={onClose}
      onSubmit={handleFormSubmit}
      overrideHeight={'420px'}
      overrideWidth={'600px'}
      submitTestId="EditPGCompatibilityModal-Submit"
      cancelTestId="EditPGCompatibilityModal-Cancel"
      buttonProps={{
        primary: {
          disabled: !canEditGFlags
        }
      }}
      submitButtonTooltip={!canEditGFlags ? RBAC_ERR_MSG_NO_PERM : ''}
    >
      <Box
        display="flex"
        width="100%"
        flexDirection="column"
        pt={2}
        pb={2}
        data-testid="EditPGCompatibilityModal-Container"
      >
        <Box className={classes.toggleContainer}>
          <Box className={classes.toggleRow}>
            <Box display={'flex'} flexDirection={'column'}>
              <Typography variant="body1">
                {t('universeActions.pgCompatibility.toggleLabel')}
              </Typography>
              <Typography className={classes.subText}>
                {t('universeActions.pgCompatibility.subText')}{' '}
              </Typography>
            </Box>
            <YBToggleField
              control={control}
              name="enablePGCompatibitilty"
              inputProps={{
                'data-testid': 'EditPGCompatibilityModal-Toggle'
              }}
            />
          </Box>
        </Box>
        {/* Enabling PG Compatibility */}
        {!isPGEnabled && pgToggleValue && (
          <Box mt={2} className={currentRF >= 3 ? classes.infoContainer : classes.errorContainer}>
            <img src={currentRF >= 3 ? InfoBlue : InfoError} alt="--" />
            <Typography variant="body2">
              <Trans
                i18nKey={
                  currentRF >= 3
                    ? 'universeActions.pgCompatibility.enableWarning'
                    : 'universeActions.pgCompatibility.enableWarningRF1'
                }
                values={{ universeName }}
              />
            </Typography>
          </Box>
        )}
        {/* Disabling PG Compatibility */}
        {isPGEnabled && !pgToggleValue && (
          <Box mt={2} className={currentRF >= 3 ? classes.infoContainer : classes.errorContainer}>
            <img src={currentRF >= 3 ? InfoBlue : InfoError} alt="--" />
            {currentRF >= 3 ? (
              <Typography variant="body2">
                <Trans i18nKey={'universeActions.pgCompatibility.disableWarning1'} /> <br />
                <ul className={classes.uList}>
                  <li>
                    <Trans
                      i18nKey={'universeActions.pgCompatibility.disableWarning2'}
                      values={{ universeName }}
                    />
                  </li>
                  <li>{t('universeActions.pgCompatibility.disableWarning3')}</li>
                </ul>
                <br />
                <Link
                  underline="always"
                  className={classes.learnLink}
                  href="https://docs.yugabyte.com/preview/explore/ysql-language-features/postgresql-compatibility/"
                  target="_blank"
                >
                  {t('common.learnMore')}
                </Link>
              </Typography>
            ) : (
              <Typography variant="body2">
                <Trans
                  values={{ universeName }}
                  i18nKey={'universeActions.pgCompatibility.disableWarning1RF1'}
                />
                <br />
                <br />
                <Trans i18nKey={'universeActions.pgCompatibility.disableWarning2RF1'} />
              </Typography>
            )}
          </Box>
        )}
      </Box>
    </YBModal>
  );
};
