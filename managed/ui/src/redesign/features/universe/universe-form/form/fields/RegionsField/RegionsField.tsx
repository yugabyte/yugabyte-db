import { ChangeEvent, ReactElement } from 'react';
import _ from 'lodash';
import { useQuery } from 'react-query';
import { useUpdateEffect } from 'react-use';
import { useTranslation } from 'react-i18next';
import { Controller, useFormContext, useWatch } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBAutoComplete } from '../../../../../../components';
import { api, QUERY_KEY } from '../../../utils/api';
import { UniverseFormData } from '../../../utils/dto';
import { PROVIDER_FIELD, REGIONS_FIELD } from '../../../utils/constants';
import { useFormFieldStyles } from '../../../universeMainStyle';

interface RegionsFieldProps {
  disabled?: boolean;
}

const getOptionLabel = (option: Record<string, string>): string => option?.name;

export const RegionsField = ({ disabled }: RegionsFieldProps): ReactElement => {
  const { control, setValue, getValues } = useFormContext<UniverseFormData>();
  const classes = useFormFieldStyles();
  const { t } = useTranslation();

  //watchers
  const provider = useWatch({ name: PROVIDER_FIELD }); //Listen to provider value change

  const { isFetching, data } = useQuery(
    [QUERY_KEY.getRegionsList, provider?.uuid],
    () => api.getRegionsList(provider?.uuid),
    {
      enabled: !!provider?.uuid, // make sure query won't run when there's no provider defined
      onSuccess: (regions) => {
        // if regions are not selected and there's single region available - automatically preselect it
        if (_.isEmpty(getValues(REGIONS_FIELD)) && regions.length === 1) {
          setValue(REGIONS_FIELD, [regions[0].uuid], { shouldValidate: true });
        }
      }
    }
  );

  const regionsList = _.isEmpty(data) ? [] : _.sortBy(data, 'name');
  const regionsListMap = _.keyBy(regionsList, 'uuid');
  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    setValue(
      REGIONS_FIELD,
      (option || []).map((item: any) => item.uuid),
      { shouldValidate: true }
    );
  };

  // reset selected regions on provider change
  useUpdateEffect(() => {
    setValue(REGIONS_FIELD, []);
  }, [provider]);

  return (
    <Box display="flex" width="100%" flexDirection={'row'} data-testid="RegionsField-Container">
      <Controller
        name={REGIONS_FIELD}
        control={control}
        rules={{
          required: t('universeForm.validation.required', {
            field: t('universeForm.cloudConfig.regionsField')
          }) as string
        }}
        render={({ field, fieldState }) => {
          const value = field.value.map((region) => regionsListMap[region]);
          return (
            <>
              <YBLabel dataTestId="RegionsField-Label">
                {t('universeForm.cloudConfig.regionsField')}
              </YBLabel>
              <Box flex={1} className={classes.defaultTextBox}>
                <YBAutoComplete
                  multiple={true}
                  loading={isFetching}
                  filterSelectedOptions={true}
                  value={(value as unknown) as string[]}
                  options={(regionsList as unknown) as Record<string, string>[]}
                  getOptionLabel={getOptionLabel}
                  onChange={handleChange}
                  disabled={disabled}
                  ybInputProps={{
                    error: !!fieldState.error,
                    helperText: fieldState.error?.message,
                    'data-testid': 'RegionsField-AutoComplete',
                    placeholder: _.isEmpty(value)
                      ? t('universeForm.cloudConfig.placeholder.selectRegions')
                      : ''
                  }}
                />
              </Box>
            </>
          );
        }}
      />
    </Box>
  );
};
