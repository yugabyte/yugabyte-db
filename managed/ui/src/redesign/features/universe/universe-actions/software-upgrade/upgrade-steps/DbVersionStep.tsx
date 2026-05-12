import { ChangeEvent } from 'react';
import { makeStyles, Typography } from '@material-ui/core';
import { YBButton } from '@yugabyte-ui-library/core';
import { Controller, useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useMutation } from 'react-query';
import { AxiosError } from 'axios';

import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { startSoftwareUpgrade } from '@app/v2/api/universe/universe';
import { YBAutoComplete } from '@app/redesign/components';
import { DbUpgradeInfoCard } from '../DbUpgradeInfoCard';
import { DbReleaseAutocompleteOption } from '../DbReleaseAutocompleteOption';
import type { DBUpgradeFormFields, ReleaseOption } from '../types';
import { useDbUpgradeModalContext } from '../DbUpgradeModalContext';
import { useRefreshUniverseTasksCache } from '@app/redesign/helpers/cacheUtils';
import { handleServerError } from '@app/utils/errorHandlingUtils';

const useStyles = makeStyles((theme) => ({
  formFieldsContainer: {
    flex: 1,

    display: 'flex',
    flexDirection: 'column',

    overflow: 'auto'
  },
  sectionHeader: {
    padding: theme.spacing(2.5, 3),

    color: theme.palette.ybacolors?.labelBackground ?? '#0B1117',
    fontSize: 15,
    fontWeight: 600,
    lineHeight: '20px'
  },
  sectionBody: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    padding: theme.spacing(1.5, 3, 3, 3)
  },
  fieldRow: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(FIELD_ROW_GAP_UNITS),

    marginTop: theme.spacing(0.5)
  },
  fieldLabel: {
    flexShrink: 0,

    width: FIELD_LABEL_WIDTH,

    fontSize: 12,
    fontWeight: 400
  },
  dropdownContainer: {
    flex: '1 1 auto',

    maxWidth: 340
  },
  upgradeInfoCardContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    marginLeft: theme.spacing(FIELD_ROW_GAP_UNITS) + FIELD_LABEL_WIDTH
  }
}));

const TEST_ID_PREFIX = 'DbVersionStep';
const FIELD_LABEL_WIDTH = 100;
const FIELD_ROW_GAP_UNITS = 4;

export const DbVersionStep = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.upgradeModal.dbVersionStep'
  });
  const classes = useStyles();
  const { currentUniverseUuid, currentDbVersion, targetReleaseOptions, closeModal } =
    useDbUpgradeModalContext();
  const refreshUniverseTasksCache = useRefreshUniverseTasksCache(currentUniverseUuid);

  const softwareUpgradeRbacAccessRequiredOn = {
    onResource: currentUniverseUuid,
    ...ApiPermissionMap.UPGRADE_NEW_UNIVERSE_SOFTWARE
  };

  const runDbUpgradePrecheck = useMutation(
    (targetDbVersion: string) =>
      startSoftwareUpgrade(currentUniverseUuid, {
        version: targetDbVersion,
        run_only_prechecks: true
      }),
    {
      onSuccess: () => {
        refreshUniverseTasksCache();
        closeModal();
      },
      onError: (error: Error | AxiosError) => {
        handleServerError(error, {
          customErrorLabel: t('toast.dbUpgradePrecheckFailed', {
            keyPrefix: 'universeActions.dbUpgrade.upgradeModal'
          })
        });
      }
    }
  );
  const { control, watch, setValue, formState } = useFormContext<DBUpgradeFormFields>();
  const selectedVersion = watch('targetDbVersion');
  const isFormDisabled = formState.isSubmitting;

  const handleVersionChange = (
    _: ChangeEvent<{}>,
    value: Record<string, string> | string | (Record<string, string> | string)[] | null
  ) => {
    const option = value as ReleaseOption | null;
    setValue('targetDbVersion', option?.version ?? '', { shouldValidate: true });
  };

  const handleRunPreupgradeCheck = () => {
    runDbUpgradePrecheck.mutate(selectedVersion);
  };

  return (
    <div className={classes.formFieldsContainer}>
      <Typography className={classes.sectionHeader} variant="h5">
        {t('stepTitle')}
      </Typography>

      <div className={classes.sectionBody}>
        <Typography variant="body2">{t('stepDescription')}</Typography>

        <div className={classes.fieldRow}>
          <Typography className={classes.fieldLabel}>{t('currentVersion')}</Typography>
          <Typography variant="body2">{currentDbVersion}</Typography>
        </div>

        <div className={classes.fieldRow}>
          <Typography className={classes.fieldLabel}>{t('targetVersion')}</Typography>
          <div className={classes.dropdownContainer}>
            <Controller
              name="targetDbVersion"
              control={control}
              rules={{ required: t('fieldRequired', { keyPrefix: 'common' }) }}
              render={({ field: { value }, fieldState }) => {
                const selectedRelease =
                  targetReleaseOptions.find((option) => option.version === value) ?? null;

                return (
                  <YBAutoComplete
                    value={selectedRelease as unknown as Record<string, string>}
                    options={targetReleaseOptions as unknown as Record<string, string>[]}
                    groupBy={(option) => option.series}
                    getOptionLabel={(option) =>
                      option.version === currentDbVersion
                        ? `${option.label} (${t('currentVersion').toLocaleLowerCase()})`
                        : option.label
                    }
                    getOptionDisabled={(option) => option.version === currentDbVersion}
                    getOptionSelected={(option, value) =>
                      value !== null && value !== undefined && option.version === value.version
                    }
                    renderOption={(option) => (
                      <DbReleaseAutocompleteOption
                        releaseOption={option as unknown as ReleaseOption}
                        currentDbVersion={currentDbVersion}
                      />
                    )}
                    onChange={handleVersionChange}
                    ybInputProps={{
                      'data-testid': `${TEST_ID_PREFIX}-VersionSelect-input`,
                      error: !!fieldState.error,
                      helperText: fieldState.error?.message,
                      id: 'dBVersion',
                      disabled: isFormDisabled
                    }}
                  />
                );
              }}
            />
          </div>
        </div>

        {selectedVersion && (
          <div className={classes.upgradeInfoCardContainer}>
            <DbUpgradeInfoCard
              currentUniverseUuid={currentUniverseUuid}
              currentVersion={currentDbVersion}
              targetVersion={selectedVersion}
            />
            <RbacValidator accessRequiredOn={softwareUpgradeRbacAccessRequiredOn} isControl>
              <YBButton
                variant="secondary"
                onClick={handleRunPreupgradeCheck}
                disabled={runDbUpgradePrecheck.isLoading}
                dataTestId={`${TEST_ID_PREFIX}-RunPreupgradeCheckButton`}
                fullWidth={false}
                sx={{ alignSelf: 'flex-end' }}
              >
                {t('runPreupgradeCheck')}
              </YBButton>
            </RbacValidator>
          </div>
        )}
      </div>
    </div>
  );
};
