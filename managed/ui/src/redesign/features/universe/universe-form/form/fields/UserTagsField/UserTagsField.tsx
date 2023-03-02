import React, { ReactElement } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, useFieldArray, FieldArrayPath } from 'react-hook-form';
import { Box, Grid, IconButton } from '@material-ui/core';
import { YBButton, YBInputField } from '../../../../../../components';
import { UniverseFormData, InstanceTag } from '../../../utils/dto';
import { USER_TAGS_FIELD } from '../../../utils/constants';
//Icons
import { ReactComponent as CloseIcon } from '../../../../../../assets/close.svg';

interface UserTagsFieldProps {
  disabled: boolean;
}

export const UserTagsField = ({ disabled }: UserTagsFieldProps): ReactElement => {
  const { t } = useTranslation();

  const { control } = useFormContext<UniverseFormData>();
  const { fields, append, remove } = useFieldArray({
    control,
    name: USER_TAGS_FIELD
  });

  return (
    <Grid container direction="column" data-testid="UserTagsField-Container">
      <Box display="flex" flexDirection="column" mb={fields?.length ? 2 : 0} gridGap="8px">
        {fields.map((field, index) => {
          return (
              <Grid key={field.id} container spacing={1} alignItems="center">
                <Grid item xs>
                  <YBInputField
                    name={`${USER_TAGS_FIELD}.${index}.name` as FieldArrayPath<InstanceTag>}
                    control={control}
                    fullWidth
                    disabled={disabled}
                    inputProps={{
                      'data-testid': `UniverseNameField-NameInput${index}`
                    }}
                  />
                </Grid>
                <Grid item xs>
                  <YBInputField
                    name={`${USER_TAGS_FIELD}.${index}.value` as FieldArrayPath<InstanceTag>}
                    control={control}
                    fullWidth
                    disabled={disabled}
                    inputProps={{
                      'data-testid': `UniverseNameField-ValueInput${index}`
                    }}
                  />
                </Grid>
                {!disabled && (
                  <Grid item>
                    <IconButton
                      color="primary"
                      data-testid={`UniverseNameField-RemoveButton${index}`}
                      onClick={() => remove(index)}
                    >
                      <CloseIcon />
                    </IconButton>
                  </Grid>
                )}
              </Grid>
          );
        })}
      </Box>
      {!disabled && (
        <Box>
          <YBButton
            variant="primary"
            data-testid={`UniverseNameField-AddTagsButton`}
            onClick={() => append({ name: '', value: '' })}
            disabled={disabled}
          >
            <span className="fa fa-plus" />
            {t('universeForm.userTags.addRow')}
          </YBButton>
        </Box>
      )}
    </Grid>
  );
};
