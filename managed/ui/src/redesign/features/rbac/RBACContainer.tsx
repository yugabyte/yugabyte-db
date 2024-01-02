/*
 * Created on Tue Jul 25 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { Tab } from "react-bootstrap";
import { useSearchParam } from "react-use";
import ManageRoles from "./roles/components/ManageRoles";
import { YBTabsPanel } from "../../../components/panels";
import { ManageUsers } from "./users/components/ManageUsers";

export const RBACContainer = () => {
    const activeTab = useSearchParam('tab') ?? 'users';
    return (
        <YBTabsPanel
            activeTab={activeTab}
            defaultTab={'role'}
            id="rbac-tab-panel"
            className="config-tabs"
        >
            <Tab
                eventKey="users"
                title="users"
                unmountOnExit
            >
                <ManageUsers />
            </Tab>
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
