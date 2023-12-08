<!--
+++
private=true
+++
-->

Perform the following steps to install `yb_voyager` using an installer script:

1. Clone the yb-voyager repository.

    ```sh
    git clone https://github.com/yugabyte/yb-voyager.git
    ```

1. Change the directory to `yb-voyager/installer_scripts`.

    ```sh
    cd yb-voyager/installer_scripts
    ```

1. Install yb-voyager using the following script:

    ```sh
    ./install-yb-voyager
    ```

    It is safe to execute the script multiple times. If the script fails, check the `/tmp/install-yb-voyager.log` file.

1. The script generates a `.yb-voyager.rc` file in the home directory. Source the file to ensure that the environment variables are set using the following command:

    ```sh
    source ~/.yb-voyager.rc
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```
