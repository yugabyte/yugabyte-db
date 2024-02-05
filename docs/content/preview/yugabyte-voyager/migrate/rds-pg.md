<!--
+++
private=true
+++
-->

1. Ensure that your database log_mode is `archivelog` as follows:

    ```sql
    SELECT LOG_MODE FROM V$DATABASE;
    ```

