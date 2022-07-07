import { Field, FormikFormProps, FormikProps } from 'formik';
import React, { useRef, useState } from 'react';

import { YBModalForm } from '../common/forms';

import * as Yup from 'yup';
import { Col, Row } from 'react-bootstrap';
import { YBCheckBox, YBFormInput } from '../common/forms/fields';

import './ConfigureMaxLagTimeModal.scss';
import { useQuery, useQueryClient } from 'react-query';
import { createAlertConfiguration, getAlertConfigurations, getAlertTemplates, updateAlertConfiguration } from '../../actions/universe';
import { YBLoading } from '../common/indicators';

const validationSchema = Yup.object().shape({
    maxLag: Yup.string().required('maximum lag is required'),
});

interface Props {
    onHide: Function;
    visible: boolean;
    currentUniverseUUID: string;
}

interface FORM_VALUES {
    maxLag: number;
    enableAlert: boolean;
}

const ALERT_NAME = 'Replication Lag';
const DEFAULT_THRESHOLD = 180000;

export function ConfigureMaxLagTimeModal({ onHide, visible, currentUniverseUUID }: Props) {

    const [alertConfigurationUUID, setAlertConfigurationUUID] = useState(null);
    const formik = useRef<any>();
    const queryClient = useQueryClient();

    const initialValues: FORM_VALUES = {
        maxLag: DEFAULT_THRESHOLD,
        enableAlert: true
    }

    const configurationFilter = {
        name: ALERT_NAME,
        targetUuid: currentUniverseUUID
    }

    const { isFetching } = useQuery(
        ['getAlertConfigurations', configurationFilter],
        () => getAlertConfigurations(configurationFilter),
        {
            enabled: visible,
            onSuccess: (data) => {
                if (Array.isArray(data) && data.length > 0) {
                    const configuration = data[0];
                    setAlertConfigurationUUID(configuration.uuid);
                    formik.current.setValues({
                        enableAlert: configuration.active,
                        maxLag: configuration.thresholds.SEVERE.threshold
                    });
                }
            }
        }
    );

    const submit = async (values: FORM_VALUES, formikBag: FormikProps<FORM_VALUES>) => {
        const templateFilter = {
            name: ALERT_NAME
        };
        const alertTemplates = await getAlertTemplates(templateFilter);
        const template = alertTemplates[0]
        template.active = values.enableAlert;
        template.thresholds.SEVERE.threshold = values.maxLag;
        template.target = {
            all: false,
            uuids: [currentUniverseUUID]
        };

        try {
            if (alertConfigurationUUID) {
                template.uuid = alertConfigurationUUID;
                await updateAlertConfiguration(template);
            } else {
                await createAlertConfiguration(template);
            }

            formikBag.setSubmitting(false);
            queryClient.invalidateQueries(['getConfiguredThreshold', configurationFilter]);
            onHide();
        } catch (error) {
            formikBag.setSubmitting(false);
        }
    };

    return (
        <YBModalForm
            visible={visible}
            title="Define Max Acceptable Lag Time"
            validationSchema={validationSchema}
            onFormSubmit={submit}
            initialValues={initialValues}
            submitLabel="Save"
            onHide={onHide}
            showCancelButton
            render={(formikProps: FormikFormProps) => {
                formik.current = formikProps;

                return isFetching ? (
                    <YBLoading />
                ) : (
                    <div className='maxLagForm'>
                        <Row className='marginTop'>
                            <Col lg={12}>
                                <Field
                                    name="maxLag"
                                    placeholder="Maximum acceptable lag in milliseconds"
                                    label="Define maximum acceptable replication lag time for this universe in milliseconds"
                                    component={YBFormInput}
                                />
                            </Col>
                        </Row>
                        <Row className='marginTop'>
                            <Col lg={12}>
                                <Field
                                    name="enableAlert"
                                    label={<span className='checkbox-label'>Enable notification</span>}
                                    component={YBCheckBox}
                                    checkState={(formikProps as any).values['enableAlert']}
                                />
                                <br />
                                <span className='alert-subtext'>We'll email you if a replication lag exceeds the defined value above</span>
                            </Col>
                        </Row>
                    </div>
                )
            }}

        />
    );
}