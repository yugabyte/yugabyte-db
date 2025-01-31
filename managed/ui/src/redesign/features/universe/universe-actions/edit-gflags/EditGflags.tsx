import { FC, useState, useRef, FocusEvent } from 'react';
import _ from 'lodash';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { useUpdateEffect } from 'react-use';
import { useForm, FormProvider } from 'react-hook-form';
import { toast } from 'react-toastify';
import { Box, Tooltip, Typography, makeStyles } from '@material-ui/core';
import { InheritRRDialog } from './InheritRRDialog';
import { ModifiedFlagsDialog } from './ModifiedFlagsDialog';
import { YBModal, YBToggle, YBRadioGroupField, YBInputField } from '../../../../components';
import { api } from '../../../../features/universe/universe-form/utils/api';
import { Universe } from '../../universe-form/utils/dto';
import {
  getAsyncCluster,
  getCurrentVersion,
  getPrimaryCluster,
  transformFlagArrayToObject,
  createErrorMessage
} from '../../universe-form/utils/helpers';
import { TOAST_AUTO_DISMISS_INTERVAL } from '../../universe-form/utils/constants';
import {
  transformToEditFlagsForm,
  EditGflagsFormValues,
  UpgradeOptions,
  EditGflagPayload
} from './GflagHelper';
import { GFlagsField } from '../../universe-form/form/fields';
import { useFormMainStyles } from '../../universe-form/universeMainStyle';
import { RBAC_ERR_MSG_NO_PERM } from '../../../rbac/common/validator/ValidatorUtils';
import { hasNecessaryPerm } from '../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../rbac/ApiAndUserPermMapping';
import { isPGEnabledFromIntent } from '../../universe-form/utils/helpers';

import InfoMessageIcon from '../../../../../redesign/assets/info-message.svg';

interface EditGflagsModalProps {
  open: boolean;
  onClose: () => void;
  universeData: Universe;
  isGFlagMultilineConfEnabled: boolean;
}

export const useStyles = makeStyles((theme) => ({
  toggleContainer: {
    display: 'flex',
    flexShrink: 1,
    flexDirection: 'row',
    alignItems: 'center',
    padding: theme.spacing(2),
    border: '1px solid #E5E5E9',
    borderRadius: theme.spacing(1),
    justifyContent: 'space-between'
  },
  defaultBox: {
    height: '20px',
    width: '50px',
    padding: theme.spacing(0.25, 0.75),
    background: '#F0F4F7',
    border: '1px solid #E9EEF2',
    borderRadius: theme.spacing(0.5),
    marginLeft: theme.spacing(1),
    marginRight: theme.spacing(2.5)
  },
  noteText: {
    fontSize: '12px',
    fontWeight: 400,
    color: '#67666C'
  },
  tabMain: {
    display: 'flex',
    flexShrink: 1,
    flexDirection: 'row',
    alignItems: 'center',
    borderBottom: '1px solid #E5E5E9'
  },
  tabContainer: {
    display: 'flex',
    flexShrink: 1,
    padding: theme.spacing(2, 0),
    alignItems: 'center',
    cursor: 'pointer',
    margin: theme.spacing(0, 1)
  },
  tooltip: {
    marginLeft: theme.spacing(1)
  },
  gFlagUpdateOptions: {
    '& .MuiFormControlLabel-root': {
      alignItems: 'flex-start'
    }
  }
}));

export const EditGflagsModal: FC<EditGflagsModalProps> = ({
  open,
  onClose,
  universeData,
  isGFlagMultilineConfEnabled
}) => {
  const { t } = useTranslation();
  const { universeDetails, universeUUID, rollMaxBatchSize } = universeData;
  const { nodePrefix } = universeDetails;
  const [isPrimary, setIsPrimary] = useState(true);
  const [openInheritRRModal, setOpenInheritRRModal] = useState(false);
  const [openWarningModal, setWarningModal] = useState(false);
  const classes = useStyles();
  const globalClasses = useFormMainStyles();
  const asyncCluster = getAsyncCluster(universeDetails);
  const primaryCluster = _.cloneDeep(getPrimaryCluster(universeDetails));
  const asyncClusterCopy = _.cloneDeep(asyncCluster);
  const currentVersion = getCurrentVersion(universeDetails) || '';
  const { gFlags, asyncGflags, inheritFlagsFromPrimary } = transformToEditFlagsForm(universeData);
  const initialGflagSet = useRef({ gFlags, asyncGflags, inheritFlagsFromPrimary });
  const isPGSupported = primaryCluster?.userIntent
    ? isPGEnabledFromIntent(primaryCluster.userIntent)
    : false;

  const formMethods = useForm<EditGflagsFormValues>({
    defaultValues: {
      gFlags: gFlags,
      inheritFlagsFromPrimary: inheritFlagsFromPrimary ?? true,
      asyncGflags: asyncGflags,
      upgradeOption: UpgradeOptions.Rolling,
      timeDelay: 180,
      numNodesToUpgradePrimary: 1
    },
    mode: 'onChange',
    reValidateMode: 'onChange'
  });

  const { control, watch, handleSubmit, setValue } = formMethods;
  const upgradeOption = watch('upgradeOption');
  const inheritFromPrimary = watch('inheritFlagsFromPrimary');
  const primaryFlags = watch('gFlags');
  const asyncFlags = watch('asyncGflags');

  const isRollingUpgrade = upgradeOption === UpgradeOptions.Rolling;
  const isNotRuntime = () => {
    if (!isPrimary) return asyncFlags?.some((f) => !f?.tags?.includes('runtime'));
    else return primaryFlags.some((f) => !f?.tags?.includes('runtime'));
  };

  const handleFormSubmit = handleSubmit(async (values) => {
    const newUniverseData = await api.fetchUniverse(universeUUID);
    const newGflagSet = transformToEditFlagsForm(newUniverseData);

    if (_.isEqual(initialGflagSet.current, newGflagSet) || openWarningModal) {
      setWarningModal(false);
      const { gFlags, asyncGflags, inheritFlagsFromPrimary } = values;
      const payload: EditGflagPayload = {
        nodePrefix,
        universeUUID,
        sleepAfterMasterRestartMillis: values.timeDelay * 1000,
        sleepAfterTServerRestartMillis: values.timeDelay * 1000,
        taskType: 'GFlags',
        upgradeOption: values?.upgradeOption,
        ybSoftwareVersion: currentVersion,
        clusters: []
      };

      if (isRollingUpgrade) {
        payload.rollMaxBatchSize = {
          primaryBatchSize: values.numNodesToUpgradePrimary ?? rollMaxBatchSize.primaryBatchSize,
          readReplicaBatchSize:
            values.numNodesToUpgradePrimary ?? rollMaxBatchSize.readReplicaBatchSize
        };
      }

      if (primaryCluster && !_.isEmpty(primaryCluster)) {
        const { masterGFlags, tserverGFlags } = transformFlagArrayToObject(gFlags);
        primaryCluster.userIntent.specificGFlags = {
          ...primaryCluster.userIntent.specificGFlags,
          inheritFromPrimary: false,
          perProcessFlags: {
            value: {
              MASTER: masterGFlags,
              TSERVER: tserverGFlags
            }
          }
        };
        delete primaryCluster.userIntent.masterGFlags;
        delete primaryCluster.userIntent.tserverGFlags;
        payload.clusters = [primaryCluster];
      }
      if (asyncClusterCopy && !_.isEmpty(asyncClusterCopy)) {
        if (inheritFlagsFromPrimary) {
          asyncClusterCopy.userIntent.specificGFlags = {
            inheritFromPrimary: true,
            perProcessFlags: {}
          };
        } else {
          const { tserverGFlags } = transformFlagArrayToObject(asyncGflags);
          asyncClusterCopy.userIntent.specificGFlags = {
            inheritFromPrimary: false,
            perProcessFlags: {
              value: {
                TSERVER: tserverGFlags
              }
            }
          };
        }
        delete asyncClusterCopy.userIntent.masterGFlags;
        delete asyncClusterCopy.userIntent.tserverGFlags;
        payload.clusters.push(asyncClusterCopy);
      }
      try {
        await api.upgradeGflags(payload, universeUUID);
        onClose();
      } catch (error) {
        toast.error(createErrorMessage(error), { autoClose: TOAST_AUTO_DISMISS_INTERVAL });
      }
    } else {
      setWarningModal(true);
    }
  });

  const handleInheritFlagsToggle = (event: any) => {
    if (event.target.checked) {
      if (_.isEmpty(asyncFlags)) setValue('inheritFlagsFromPrimary', true);
      else setOpenInheritRRModal(true);
    } else setValue('inheritFlagsFromPrimary', false);
  };

  useUpdateEffect(() => {
    if (inheritFromPrimary) setIsPrimary(true);
  }, [inheritFromPrimary]);

  const handleNumNodeChangePrimary = (e: FocusEvent<HTMLInputElement>) => {
    const fieldValue = (e.target.value as unknown) as number;
    if (fieldValue > rollMaxBatchSize?.primaryBatchSize)
      setValue('numNodesToUpgradePrimary', rollMaxBatchSize?.primaryBatchSize);
    else if (fieldValue < 1) setValue('numNodesToUpgradePrimary', 1);
    else setValue('numNodesToUpgradePrimary', fieldValue);
  };

  const GFLAG_UPDATE_OPTIONS = [
    {
      value: UpgradeOptions.Rolling,
      label: (
        <Box display="flex" flexDirection="column" alignItems="baseline">
          <div className="upgrade-radio-label"> {t('universeForm.gFlags.rollingMsg')}</div>

          <div className="gflag-delay">
            <span className="vr-line">|</span>
            {t('universeForm.gFlags.delayBetweenServers')}&nbsp;
            <YBInputField name="timeDelay" type="number" disabled={!isRollingUpgrade} />
            &nbsp;
            {t('universeForm.gFlags.seconds')}
          </div>
          {isRollingUpgrade && rollMaxBatchSize?.primaryBatchSize > 1 && (
            <div className="gflag-num-nodes-upgrade">
              <span className="vr-line">|</span>
              {t('universeForm.gFlags.numNodesToRollingUpgrade')}&nbsp;
              <YBInputField
                name="numNodesToUpgradePrimary"
                type="number"
                inputProps={{
                  min: 1,
                  max: rollMaxBatchSize.primaryBatchSize,
                  autoFocus: true,
                  'data-testid': 'EditGFlags-NumNodesToRollingUpgrade'
                }}
                onChange={handleNumNodeChangePrimary}
              />
              <Tooltip
                className={classes.tooltip}
                title={t('universeForm.gFlags.rollingUpgradeMsg')}
                arrow
                placement="top"
              >
                <img src={InfoMessageIcon} alt="info" />
              </Tooltip>
              &nbsp;
            </div>
          )}
        </Box>
      )
    },
    {
      value: UpgradeOptions.NonRolling,
      label: (
        <Box className="upgrade-radio-label" mb={1}>
          {t('universeForm.gFlags.nonRollingMsg')}
        </Box>
      )
    },
    {
      value: UpgradeOptions.NonRestart,
      label: (
        <Box display="flex" className="upgrade-radio-label" mb={1}>
          {t('universeForm.gFlags.nonRestart') +
            `${isNotRuntime() ? t('universeForm.gFlags.nonRestartRuntime') : ''}`}
        </Box>
      )
    }
  ];

  const canEditGFlags = hasNecessaryPerm({
    onResource: universeUUID,
    ...ApiPermissionMap.UPGRADE_UNIVERSE_GFLAGS
  });

  return (
    <YBModal
      open={open}
      titleSeparator
      size="lg"
      overrideHeight={800}
      overrideWidth={1100}
      cancelLabel={t('common.cancel')}
      submitLabel={t('common.applyChanges')}
      title={t('universeForm.gFlags.title')}
      onClose={onClose}
      onSubmit={handleFormSubmit}
      submitTestId="EditGflags-Submit"
      cancelTestId="EditGflags-Close"
      buttonProps={{
        primary: {
          disabled: !canEditGFlags
        }
      }}
      submitButtonTooltip={!canEditGFlags ? RBAC_ERR_MSG_NO_PERM : ''}
    >
      <FormProvider {...formMethods}>
        <Box
          width="100%"
          display="flex"
          flexDirection="column"
          flex={1}
          height="100%"
          data-testid="EditGflags-Modal"
          style={{ scrollbarWidth: 'none' }}
        >
          {asyncClusterCopy && (
            <Box className={classes.toggleContainer}>
              <Box flex={1} display="flex" flexDirection={'row'} alignItems={'center'}>
                <YBToggle
                  onChange={handleInheritFlagsToggle}
                  disabled={!canEditGFlags}
                  inputProps={{
                    'data-testid': 'ToggleInheritFlags'
                  }}
                  checked={inheritFromPrimary}
                />
                <Typography variant="body2">{t('universeForm.gFlags.inheritFlagsMsg')}</Typography>
                <span className={classes.defaultBox}>
                  <Typography variant="subtitle1">Default</Typography>
                </span>
              </Box>
              <Box display="flex">
                <Typography className={classes.noteText}>
                  <b>{t('universeForm.gFlags.note')}</b> &nbsp;
                  {t('universeForm.gFlags.rrWithOnlyTserver')}
                </Typography>
              </Box>
              <Box></Box>
            </Box>
          )}
          {asyncClusterCopy && !inheritFromPrimary && (
            <Box className={classes.tabMain}>
              <Box
                onClick={() => setIsPrimary(true)}
                className={clsx(
                  classes.tabContainer,
                  isPrimary ? globalClasses.selectedTab : globalClasses.disabledTab
                )}
              >
                {t('universeForm.gFlags.primaryTab')}
              </Box>
              <Box
                onClick={() => setIsPrimary(false)}
                className={clsx(
                  classes.tabContainer,
                  !isPrimary ? globalClasses.selectedTab : globalClasses.disabledTab
                )}
                ml={2}
                mr={1}
              >
                {t('universeForm.gFlags.rrTab')}
              </Box>
            </Box>
          )}
          <Box display="flex" flexDirection="column" height="100%" mt={2} flex={1}>
            {isPrimary && (
              <GFlagsField
                control={control}
                dbVersion={currentVersion}
                editMode={true}
                fieldPath={'gFlags'}
                isReadReplica={false}
                isReadOnly={!canEditGFlags}
                tableMaxHeight={
                  !asyncClusterCopy ? '420px' : inheritFromPrimary ? '362px' : '296px'
                }
                isGFlagMultilineConfEnabled={isGFlagMultilineConfEnabled}
                isPGSupported={isPGSupported}
              />
            )}
            {!isPrimary && (
              <GFlagsField
                control={control}
                dbVersion={currentVersion}
                editMode={true}
                fieldPath={'asyncGflags'}
                isReadReplica={true}
                isReadOnly={!canEditGFlags}
                tableMaxHeight={
                  !asyncClusterCopy ? '412px' : inheritFromPrimary ? '354px' : '288px'
                }
                isGFlagMultilineConfEnabled={isGFlagMultilineConfEnabled}
                isPGSupported={isPGSupported}
              />
            )}
          </Box>
          <Box display="flex" flexShrink={1} className="gflag-upgrade-container">
            <Box display="flex" flexShrink={1} className="gflag-upgrade--label">
              <span>{t('universeForm.gFlags.gFlagUpdateOptions')}</span>
            </Box>
            <div className="gflag-upgrade-options">
              <YBRadioGroupField
                name="upgradeOption"
                options={GFLAG_UPDATE_OPTIONS}
                control={control}
                orientation="vertical"
                className={classes.gFlagUpdateOptions}
              />
            </div>
          </Box>
        </Box>
        <InheritRRDialog
          onSubmit={() => {
            setValue('inheritFlagsFromPrimary', true);
            setOpenInheritRRModal(false);
          }}
          onCancel={() => {
            setOpenInheritRRModal(false);
          }}
          open={openInheritRRModal}
        />
        <ModifiedFlagsDialog
          onSubmit={() => {
            setWarningModal(false);
            window.location.reload();
          }}
          onCancel={handleFormSubmit}
          open={openWarningModal}
        />
      </FormProvider>
    </YBModal>
  );
};
