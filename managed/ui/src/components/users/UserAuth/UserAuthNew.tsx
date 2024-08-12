/*
 * Created on Thu Aug 01 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useState } from "react";
import clsx from "clsx";
import { LDAPAuthNew } from "../../../redesign/features/userAuth/ldap/LDAPAuthRedesigned";
import { OIDCAuthNew } from "../../../redesign/features/userAuth/oidc/OIDCAuthRedesigned";

const TABS = [
    {
        label: 'LDAP Configuration',
        id: 'LDAP New',
    },
    {
        label: 'OIDC Configuration',
        id: 'OIDC New',
    }
];

export const UserAuthNew = (props: any) => {
    const [activeTab, setTab] = useState(TABS[0].id);
    const handleTabSelect = (e: any, tab: any) => {
        e.stopPropagation();
        setTab(tab);
    };
    const currentTab = TABS.find((tab) => tab.id === activeTab) ?? null;

    return (
        <div className="user-auth-container">
            <>
                <div className="ua-tab-container">
                    {TABS.map(({ label, id }) => {
                        return <div
                            key={id}
                            className={clsx('ua-tab-item', id === activeTab && 'ua-active-tab')}
                            onClick={(e) => handleTabSelect(e, id)}
                        >
                            {label}
                        </div>;
                    })}
                </div>
                <div className="ua-sec-divider" />
            </>

            <div className={clsx('pl-15')}>
                {
                    currentTab?.id === 'LDAP New' ? <LDAPAuthNew {...props} /> : <OIDCAuthNew {...props} />
                }
            </div>
        </div>);
};
