# AutoFlags

AutoFlags are gFlags with two hard-coded values: Initial and Target (instead of the regular gFlag Default). AutoFlags have two states: promoted and not-promoted. An AutoFlag is set to its Initial value in the not-promoted state and to its Target value in the promoted state.

- New universes will start with all AutoFlags in the promoted state (Target value).
- When a process undergoes an upgrade, all new AutoFlags will be in the not-promoted state (Initial value). They will get promoted only after all the yb-master and yb-tserver processes in the universe (and other external processes like CDCServers and xClusters for class kExternal) have been upgraded.
- Custom overrides via command line or the flags file has higher precedence and takes effect immediately.
- AutoFlags cannot be renamed or removed. If the flag definition is moved to a different file, then make sure the new file is included in the same processes as before.
- At build time, an `auto_flags.json` file is created in the bin directory. This file has the list of all AutoFlags in the yb-master and yb-tserver processes.

## When to use AutoFlags

A Workflow is the series of activities that are necessary to complete a task (ex: user issuing a DML, tablet split, load balancing). They can be simple and confined to a function block or involve coordination between multiple universes. Workflows that add or modify the format of data that is sent over the wire to another process or is persisted on disk require special care. The consumer of the data and its persistence determines when the new workflow can be safely enabled after an upgrade, and whether it is safe to perform rollbacks or downgrades after they are enabled. AutoFlags are required to safely and automatically enable such workflows.

For example, `yb_enable_expression_pushdown` requires all yb-tservers to run a version which can understand and respond to the new request type. So, it can only be enabled after all yb-tservers in the universe have upgraded to a supported code version. Using a regular gFlag, we have to set the default value to `false` so that we don't start using it during an upgrade. After the upgrade, customer will have to manually set it to `true`. An AutoFlag will have initial value `false` and target value `true` and will not require customers to manually set it.

## How to add a new AutoFlag

New AutoFlags are defined using the following syntax in the primary cpp file where their value is used.

`DEFINE_RUNTIME_AUTO_<value_type>(<flag_name>, <flag_class>, <initial_value> , <target_value>, "<usage>");`

- value_type: [`bool`, `int32`, `int64`, `uint64`, `double`, `string`]
- flag_name: A friendly descriptive name for the AutoFlag.
- flag_class: [`kLocalVolatile`, `kLocalPersisted`, `kExternal`, `kNewInstallsOnly`]
- initial_value: The initial value of type `<value_type>`.
- target_value: The target value of type `<value_type>`.
- usage: Usage information about the AutoFlag.

Ex:  
`DEFINE_RUNTIME_AUTO_bool(fun_with_flags, kLocalPersisted, /* initial_value */ false, /* target_value */ true, "Vexillology is the study of flags.");`


If you need to use the AutoFlag in additional files then you can declare them  using the following syntax.

`DECLARE_<value_type>(<flag_name>);`

- value_type: [`bool`, `int32`, `int64`, `uint64`, `double`, `string`] Must match the one used to define the AutoFlag.
- flag_name: A friendly descriptive name for the AutoFlag.

Ex:  
`DECLARE_bool(fun_with_flags);`

Postgres AutoFlags require an AutoFlag and a GUC variable with the same name, value and description. These AutoFlags use a slightly different macro.

`DEFINE_RUNTIME_AUTO_PG_FLAG(<value_type>, <flag_name>, <flag_class>, <initial_value> , <target_value>, "<usage>");`

Ex:
`DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_enable_expression_pushdown, kLocalVolatile, false, true, "Push supported expressions from ysql down to DocDB for evaluation.");`

## How to choose the AutoFlag class

AutoFlag class is picked based on the persistence property of the data, and which processes use it.
1. LocalVolatile:  
    Adds/modifies format of data that may be sent over the wire to another process within the same universe. No new/modification to the format of persisted data.
2. LocalPersisted:  
    Adds/modifies format of data that may be persisted but used only within the same universe.
3. External:  
    Adds/modifies format of data that may be used outside the universe. (Example of such processes: xCluster, Backups, CDCSDK Server, ...)

## When is the AutoFlag promoted

| AutoFlag class    | When to Promote     | Safe to demote | Examples  |
| ---               | ---                   | ---               | ---      |
| LocalVolatile     | After all the processes in our universe have been upgraded to the new code version.   | Yes | yb_enable_expression_pushdown |
| LocalPersisted    | After all the processes in our universe have been upgraded to the new code version.   | No | enable_flush_retryable_requests |
| External          | After all the processes in our universe and other dependent universes and processes have been upgraded to the new code version.   | No | regular_tablets_data_block_key_value_encoding, enable_stream_compression |
| NewInstallsOnly   | No promotion after upgrades.  | No | TEST_auto_flags_new_install |

>Note:  
>String flags are not Runtime safe. Avoid these until #16593 is fixed.
