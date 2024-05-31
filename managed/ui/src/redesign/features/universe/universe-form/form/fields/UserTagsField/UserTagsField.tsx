import { ReactElement } from 'react';
import _ from 'lodash';
import { useTranslation } from 'react-i18next';
import { useFormContext, useFieldArray, FieldArrayPath } from 'react-hook-form';
import { Box, IconButton } from '@material-ui/core';
import { YBButton, YBInputField } from '../../../../../../components';
import { UniverseFormData, InstanceTag } from '../../../utils/dto';
import { USER_TAGS_FIELD } from '../../../utils/constants';
//Icons
import { CloseSharp } from '@material-ui/icons';
import { useFormFieldStyles } from '../../../universeMainStyle';

interface UserTagsFieldProps {
  disabled: boolean;
  isAsyncCluster?: boolean;
}

export const UserTagsField = ({ disabled, isAsyncCluster }: UserTagsFieldProps): ReactElement => {
  const { t } = useTranslation();
  const classes = useFormFieldStyles();

  const { control } = useFormContext<UniverseFormData>();
  const { fields, append, remove } = useFieldArray({
    control,
    name: USER_TAGS_FIELD
  });

  return (
    <Box
      display="flex"
      flexDirection="column"
      mb={fields?.length ? 2 : 0}
      data-testid="UserTagsField-Container"
    >
      {fields.map((field, index) => {
        if (isAsyncCluster && (_.isEmpty(field.name) || _.isEmpty(field.value))) return null;
        return (
          <Box key={field.id} display="flex" flexDirection="row" mb={1}>
            <Box display="flex" className={classes.defaultTextBox} mr={1}>
              <YBInputField
                name={`${USER_TAGS_FIELD}.${index}.name` as FieldArrayPath<InstanceTag>}
                control={control}
                fullWidth
                disabled={disabled}
                placeholder={t('universeForm.userTags.tagName')}
                inputProps={{
                  'data-testid': `UniverseNameField-NameInput${index}`
                }}
              />
            </Box>
            <Box display="flex" className={classes.defaultTextBox} mr={1}>
              <YBInputField
                name={`${USER_TAGS_FIELD}.${index}.value` as FieldArrayPath<InstanceTag>}
                control={control}
                fullWidth
                disabled={disabled}
                placeholder={t('universeForm.userTags.tagValue')}
                inputProps={{
                  'data-testid': `UniverseNameField-ValueInput${index}`
                }}
              />
            </Box>
            {!disabled && (
              <Box display="flex" flexShrink={1}>
                <IconButton
                  color="default"
                  size="medium"
                  data-testid={`UniverseNameField-RemoveButton${index}`}
                  onClick={() => remove(index)}
                >
                  <CloseSharp />
                </IconButton>
              </Box>
            )}
          </Box>
        );
      })}
      {!disabled && (
        <Box>
          <YBButton
            variant="primary"
            data-testid={`UniverseNameField-AddTagsButton`}
            onClick={() => append({ name: '', value: '' })}
            size="medium"
            disabled={disabled}
          >
            <span className="fa fa-plus" />
            {t('universeForm.userTags.addRow')}
          </YBButton>
        </Box>
      )}
    </Box>
  );
};
