/*
 * Created on Tue Jul 25 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useState } from "react";
import { Tab } from "react-bootstrap";
import ManageRoles from "./roles/components/ManageRoles";
import { YBTabsPanel } from "../../../components/panels";

export const RBACContainer = () => {
    const [activeTab, setActiveTable] = useState('role');
    return (
        <YBTabsPanel
            activeTab={activeTab}
            defaultTab={'role'}
            id="rbac-tab-pabel"
            className="config-tabs"
        >
            <Tab
                eventKey="role"
                title={"Role"}
                unmountOnExit
            >
                <ManageRoles />
            </Tab>
        </YBTabsPanel>
    );
};
