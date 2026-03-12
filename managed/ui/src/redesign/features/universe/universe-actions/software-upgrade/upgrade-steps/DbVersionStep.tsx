import { ChangeEvent } from 'react';
import { useFormContext, Controller } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { makeStyles, Typography } from '@material-ui/core';

import { YBAutoComplete, YBButton } from '@yugabyte-ui-library/core';

import { DbUpgradeInfoCard } from '@app/redesign/features/universe/universe-actions/software-upgrade/DbUpgradeInfoCard';
import { DbReleaseAutocompleteOption } from '@app/redesign/features/universe/universe-actions/software-upgrade/DbReleaseAutocompleteOption';
import type {
  DBUpgradeFormFields,
  ReleaseOption
} from '@app/redesign/features/universe/universe-actions/software-upgrade/types';

const TRANSLATION_KEY_PREFIX = 'universeActions.dbUpgrade.upgradeModal.dbVersionStep';

const FIELD_LABEL_WIDTH = 100;
const FIELD_ROW_GAP_UNITS = 4;

const useStyles = makeStyles((theme) => ({
  formFieldsContainer: {
    flex: 1,

    display: 'flex',
    flexDirection: 'column',

    overflow: 'auto'
  },
  sectionHeader: {
    padding: theme.spacing(2.5, 3),

    borderBottom: `1px solid ${theme.palette.grey[200]}`,
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

interface DbVersionStepProps {
  currentRelease: string;
  targetReleaseOptions: ReleaseOption[];
  currentUniverseUuid: string;
}

const TEST_ID_PREFIX = 'DbVersionStep';

export const DbVersionStep = ({
  currentRelease,
  targetReleaseOptions,
  currentUniverseUuid
}: DbVersionStepProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();

  const { control, watch, setValue, formState } = useFormContext<DBUpgradeFormFields>();
  const selectedVersion = watch('targetDbVersion');
  const isFormDisabled = formState.isSubmitting;

  const handleVersionChange = (
    _: ChangeEvent<{}>,
    value: string | Record<string, string> | (string | Record<string, string>)[] | null
  ) => {
    const option = value as ReleaseOption | null;
    setValue('targetDbVersion', option?.version ?? '', { shouldValidate: true });
  };

  const handleRunPreupgradeCheck = () => {
    // TODO: Implement pre-upgrade check
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
          <Typography variant="body2">{currentRelease}</Typography>
        </div>

        <div className={classes.fieldRow}>
          <Typography className={classes.fieldLabel}>{t('targetVersion')}</Typography>
          <div className={classes.dropdownContainer}>
            <Controller
              name="targetDbVersion"
              control={control}
              rules={{ required: t('fieldRequired', { keyPrefix: 'common' }) }}
              render={({ field: { value }, fieldState }) => {
                return (
                  <YBAutoComplete
                    value={value}
                    options={targetReleaseOptions as unknown as Record<string, string>[]}
                    groupBy={(option) => option.series}
                    getOptionLabel={(option) =>
                      typeof option === 'string' ? option : (option.label ?? option.version)
                    }
                    getOptionDisabled={(option) => option.version === currentRelease}
                    dataTestId={`${TEST_ID_PREFIX}-VersionSelect`}
                    renderOption={(props, option) => (
                      <li {...props}>
                        <DbReleaseAutocompleteOption
                          releaseOption={option as unknown as ReleaseOption}
                        />
                      </li>
                    )}
                    onChange={handleVersionChange}
                    ybInputProps={{
                      dataTestId: `${TEST_ID_PREFIX}-VersionSelect-input`,
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
              currentVersion={currentRelease}
              targetVersion={selectedVersion}
            />
            <YBButton
              variant="secondary"
              onClick={handleRunPreupgradeCheck}
              dataTestId={`${TEST_ID_PREFIX}-RunPreupgradeCheckButton`}
              fullWidth={false}
              sx={{ alignSelf: 'flex-end' }}
            >
              {t('runPreupgradeCheck')}
            </YBButton>
          </div>
        )}
      </div>
    </div>
  );
};
