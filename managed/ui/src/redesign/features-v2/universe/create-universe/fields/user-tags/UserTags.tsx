/* eslint-disable react/display-name */
/*
 * Created on Mon Nov 13 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useFormContext, useFieldArray, FieldArrayPath } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { mui, YBInputField, YBButton } from '@yugabyte-ui-library/core';
import { OtherAdvancedProps, InstanceTag } from '../../steps/advanced-settings/dtos';
import { StyledLink } from '../../components/DefaultComponents';

const { Box, styled, Typography, IconButton } = mui;

import CloseIcon from '../../../../../assets/close-v2.svg';
import CircleAddIcon from '../../../../../assets/circle-add-v2.svg';

interface UserTagsProps {
  disabled: boolean;
}

const USER_TAGS_FIELD = 'instanceTags';

const StyledSubText = styled(Typography)(({ theme }) => ({
  fontSize: 13,
  lineHeight: '20px',
  fontWeight: 400,
  color: '#4E5F6D',
  marginBottom: '24px'
}));

export const UserTagsField: FC<UserTagsProps> = ({ disabled }) => {
  const { setValue, control } = useFormContext<OtherAdvancedProps>();

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.otherAdvancedSettings.userTags'
  });

  const { fields, append, remove } = useFieldArray({
    control,
    name: USER_TAGS_FIELD
  });

  return (
    <Box sx={{ display: 'flex', width: '100%', flexDirection: 'column', pt: 1, pb: 3 }}>
      <StyledSubText>
        {t('subText')}
        <StyledLink>{t('learnMore')}</StyledLink>
      </StyledSubText>
      {fields.map((field, index) => {
        return (
          <Box
            sx={{ display: 'flex', flexDirection: 'row', gap: '8px', mb: 2, alignItems: 'end' }}
            key={field.id}
          >
            <YBInputField
              name={`${USER_TAGS_FIELD}.${index}.name` as FieldArrayPath<InstanceTag>}
              sx={{ width: '440px' }}
              label={index === 0 ? t('tagName') : null}
              control={control}
              placeholder={t('tagNamePlaceholder')}
              dataTestId={`user-tags-field-name-${index}`}
            />
            <YBInputField
              name={`${USER_TAGS_FIELD}.${index}.value` as FieldArrayPath<InstanceTag>}
              sx={{ width: '440px' }}
              label={index === 0 ? t('tagValue') : null}
              control={control}
              placeholder={t('tagValuePlaceholder')}
              dataTestId={`user-tags-field-value-${index}`}
            />
            <IconButton
              color="default"
              data-testid={`UniverseNameField-RemoveButton${index}`}
              onClick={() => remove(index)}
            >
              <CloseIcon />
            </IconButton>
          </Box>
        );
      })}
      <Box sx={{ mt: 1, display: 'flex' }}>
        <YBButton
          variant="secondary"
          data-testid={`UniverseNameField-AddTagsButton`}
          onClick={() => append({ name: '', value: '' })}
          size="medium"
          disabled={disabled}
          startIcon={<CircleAddIcon />}
          dataTestId="user-tags-field-add-button"
        >
          {t('addTag')}
        </YBButton>
      </Box>
    </Box>
  );
};
