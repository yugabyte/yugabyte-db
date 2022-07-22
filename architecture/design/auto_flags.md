# Auto Flags

AutoFlags are gFlags with two hard coded values, Initial and Target (Instead of the regular gFlag Default). The AutoFlag is considered not-promoted when set to Initial value, and promoted when set to Target value. The value of the AutoFlag is assigned based on the state of the system.
- New universes will start with all AutoFlags set to Target value.
- When a process undergoes an upgrade, new AutoFlags are set to Initial value. Once all the other dependent processes are upgraded they will switch to the Target value.
- Custom override via command line or the flags file has a higher precedence and takes effect immediately.
- AutoFlags cannot be renamed or removed. If the flag definition is moved to a different file, then make sure the new file is included in the same processes as before.
- At build time an auto_flags.json file will be created in the bin directory. This has a list of all AutoFlags in all yb processes.

## When to use AutoFlags

Workflow is the series of activities that are necessary to complete a task (Ex: User issuing a DML, tablet split, load balancing). They can be simple and confined to a function block or involve coordination between multiple universes. Workflows that add or modify the format of data that is sent over the wire to another process or is persisted on disk require special care. The consumer of the data and its persistence determines when the new workflow can be enabled after an upgrade, and whether it is safe to perform rollbacks\downgrades after they get enabled.  
AutoFlags are required to safely enable such workflows.  

For example `yb_enable_expression_pushdown` requires all yb-tserver to run a version which can understand and respond to the new request. So, it can only be enabled after all yb-tservers in the universe have upgraded to a supported code version. Using a regular gFlag we have to set the default value to `false` so that we don't start using it during an upgrade. After the upgrade customer will have to manually set it to `true`. An AutoFlag will have initial value `false` and target value `true` and not require customers to explicitly set it.

## How to add a new AutoFlag

New AutoFlag are defined using the following syntax in the primary cpp file where its value is used.

`DEFINE_AUTO_<value_type>(<flag_name>, <flag_class>, INITIAL_VAL(<initial_value>) , TARGET_VAL(<target_value>), "<usage>");`

- value_type: [`bool`, `int32`, `int64`, `uint64`, `double`, `string`]
- flag_name: A friendly descriptive name for the AutoFlag.
- flag_class: [`kLocalVolatile`, `kLocalPersisted`, `kExternal`]
- initial_value: The initial value of type `<value_type>`.
- target_value: The target value of type `<value_type>`.
- usage: Usage information about the AutoFlag.

Ex:  
`DEFINE_AUTO_bool(fun_with_flags, kLocalPersisted, INITIAL_VAL(false), TARGET_VAL(true), "Vexillology is the study of flags.");`


If you need to use the AutoFlag in additional files then you can declare them  using the following syntax.

`DECLARE_<value_type>(<flag_name>);`

- value_type: [`bool`, `int32`, `int64`, `uint64`, `double`, `string`] Must match the one used to define the AutoFlag.
- flag_name: A friendly descriptive name for the AutoFlag.

Ex:  
`DECLARE_bool(fun_with_flags);`

## How to choose the AutoFlag class

AutoFlag class is picked based on the persistence property of the data, and which processes use it.
1. LocalVolatile:  
    Adds/modifies format of data that may be sent over the wire to another process within the same universe. No new\modification to the format of persisted data.
2. LocalPersisted:  
    Adds/modifies format of data that may be persisted but used only within the same universe.
3. External:  
    Adds/modifies format of data that may be used outside the universe. (Example of such processes: xCluster, Backups, CDCSDK Server, ...)

## When is the AutoFlag promoted

| AutoFlag class    | When to Promotion     | Safe to downgrade | Examples  |
| ---               | ---                   | ---               | ---      |
| LocalVolatile     | After all the processes in our universe have been upgraded to the new code version.   | Yes, after the AutoFlag is demoted [^1] | yb_enable_expression_pushdown |
| LocalPersisted    | After all the processes in our universe have been upgraded to the new code version.   | No | auto_flags_initialized |
| External          | After all the processes in our universe and other dependent universes and processes have been upgraded to the new code version.   | No | regular_tablets_data_block_key_value_encoding, enable_stream_compression |

[^1]: Not yet implemented.

>Note:  
>Non-Runtime flags require an additional restart of the process to switch their value. So, they should be avoided whenever possible.  
>String flags are not Runtime safe.
