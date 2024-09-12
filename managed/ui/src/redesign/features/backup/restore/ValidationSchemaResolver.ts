/*
 * Created on Thu Sep 05 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import * as yup from 'yup';

import { TFunction } from "i18next";
import { isPITREnabledInBackup } from './RestoreUtils';
import { KEYSPACE_VALIDATION_REGEX } from '../../../../components/backupv2/common/BackupUtils';
import { Page, RestoreContext } from "./models/RestoreContext";
import { RestoreFormModel } from "./models/RestoreFormModel";
import { TableType } from '../../../helpers/dtos';

const keyPrefix = 'backup.restore.validationErrMsg';

export const validationSchemaResolver = (restoreContext: RestoreContext, formValues: RestoreFormModel, t: TFunction) => {

    const { formProps: { currentPage }, keyspacesInTargetUniverse: tablesInTargetUniverse } = restoreContext;

    let schema = yup.object();

    switch (currentPage) {
        case Page.SOURCE:
            schema = getSourceValidationSchema(restoreContext, formValues, t);
            break;
        case Page.RENAME_KEYSPACES:
            schema = getRenameKeyspaceValidationSchema(restoreContext, tablesInTargetUniverse, t);
            break;
        case Page.TARGET:
            schema = getTargetValidationSchema(restoreContext, formValues, t);
            break;
        default:
            throw new Error('Invalid page');
    }

    try {
        schema.validateSync(formValues, { abortEarly: false });
        return {
            errors: {},
            values: formValues
        };
    }
    catch (errors: any) {
        return {
            values: {},
            errors: errors.inner.reduce(
                (allErrors: any, currentError: any) => ({
                    ...allErrors,
                    [currentError.path]: {
                        type: currentError.type ?? "validation",
                        message: currentError.message,
                    },
                }),
                {}
            )
            // : { }
        };
    }

};


const getSourceValidationSchema = (_restoreContext: RestoreContext, formValues: RestoreFormModel, t: TFunction) => {
    const { source, currentCommonBackupInfo } = formValues;
    const validationSchema = yup.object<Partial<RestoreFormModel>>({
        source: yup.object({
            keyspace: yup.object({
                label: yup.string().required(),
                value: yup.string().required(),
                isDefaultOption: yup.boolean().notRequired()
            }).typeError(t('requiredField', { keyPrefix: 'common' })).required() as any,
            pitrMillisOptions: yup.object({
                secs: yup.number().test('pitrMillisOptions.secs', t('pitrMillisOptions.secs', { keyPrefix }), (value: any) => {
                    if (!isPITREnabledInBackup(currentCommonBackupInfo!)) return true;
                    const secs = parseInt(value);
                    return secs >= 0 && secs <= 59;
                })
            })

        }) as any
    });

    return validationSchema;
};

const getTargetValidationSchema = (restoreContext: RestoreContext, formValues: RestoreFormModel, t: TFunction) => {

    const { preflightResponse } = restoreContext;

    const validationSchema = yup.object<Partial<RestoreFormModel>>({
        target: yup.object().shape({
            forceKeyspaceRename: yup.boolean(),
            parallelThreads: yup.number(),
            renameKeyspace: yup.boolean(),
            targetUniverse: yup
                .object()
                .shape({
                    label: yup.string(),
                    value: yup.string()
                })
                .typeError(t('requiredField', { keyPrefix: 'common' })),
            kmsConfig: yup
                .object()
                .nullable()
                .shape({
                    label: yup.string(),
                    value: yup.string()
                })
                .test('kmsConfig', t('kmsConfigRequired', { keyPrefix }), (value) => {
                    if (!preflightResponse) return true;
                    return !preflightResponse.hasKMSHistory ? true : !!value?.value;
                })
        }) as any
    });

    return validationSchema;
};


export const getRenameKeyspaceValidationSchema = (
    restoreContext: RestoreContext,
    tables: string[],
    t: TFunction
) => {
    const contxt: Record<string, any> = {};

    const {
        backupDetails,
        additionalBackupProps
    } = restoreContext;

    const validationSchema = yup.object<Partial<RestoreFormModel>>({
        renamedKeyspace: yup
            .array()
            .min(1)
            .of(
                yup.object().shape({
                    renamedKeyspace: yup
                        .string()
                        .matches(KEYSPACE_VALIDATION_REGEX, {
                            message: t('invalidKeyspaceName', { keyPrefix }),
                            excludeEmptyString: true
                        })
                        .when('renamedKeyspaces', {
                            // check if the given name is already present in the database
                            // we do this for all YSQL backups,
                            // and for ycql, we do if it is not a single keyspace restore.(i.e)
                            // in single keyspace restore, user is allowed to restore a particular table on same keyspace
                            is: () =>
                                backupDetails?.backupType === TableType.PGSQL_TABLE_TYPE ||
                                !additionalBackupProps?.singleKeyspaceRestore,
                            then: yup
                                .string()
                                .notOneOf(
                                    tables,
                                    t('keyspacesAlreadyExists', { keyPrefix })
                                )
                        })
                        // check if same name is given as input
                        .test(
                            'Unique',
                            t('duplicateKeyspaceName', { keyPrefix }),
                            function (value) {
                                if (!value) return true;
                                if (contxt[value] !== undefined && (this.options as any)['index'] !== contxt[value]) {
                                    return false;
                                }
                                contxt[value] = (this.options as any)['index'];
                                return true;
                            }
                        )
                })
            ) as any
    });
    return validationSchema;
};
