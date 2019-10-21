## Prerequisites

1. <i class="fab fa-apple" aria-hidden="true"></i> macOS 10.12 or later.

2. Verify that you have Python 2 installed. Support for Python 3 is in the works.

    ```sh
    $ python --version
    ```

    ```
    Python 2.7.10
    ```

3. `wget` or `curl` is available.

    The instructions use the `wget` command to download files. If you prefer to use `curl` (included in macOS), you can replace `wget` with `curl -O`.

    To install `wget` on your Mac, you can run the following command if you use Homebrew:

    ```sh
    $ brew install wget
    ```     

4. Each tablet maps to its own file, so if you experiment with a few hundred tables and a few tablets per table, you can soon end up creating a large number of files in the current shell. Make sure that this command shows a big enough value.

    ```sh
    $ launchctl limit maxfiles
    ```

    We recommend setting the soft and hard limits to 1048576.

    Edit `/etc/sysctl.conf`, if it exists, to include the following:

    ```sh
    kern.maxfiles=1048576
    kern.maxproc=2500
    kern.maxprocperuid=2500
    kern.maxfilesperproc=1048576
    ```

    If this file does not exist, then create the file `/Library/LaunchDaemons/limit.maxfiles.plist` and insert the following:

    ```xml
    <?xml version="1.0" encoding="UTF-8"?>
    <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
      <plist version="1.0">
        <dict>
          <key>Label</key>
            <string>limit.maxfiles</string>
          <key>ProgramArguments</key>
            <array>
              <string>launchctl</string>
              <string>limit</string>
              <string>maxfiles</string>
              <string>1048576</string>
              <string>1048576</string>
            </array>
          <key>RunAtLoad</key>
            <true/>
          <key>ServiceIPC</key>
            <false/>
        </dict>
      </plist>
    ```

    Enure that the `plist` file is owned by `root:wheel` and has permissions `-rw-r--r--`. To take effect, you need to reboot your computer or run this command:

    ```sh
    $ sudo launchctl load -w /Library/LaunchDaemons/limit.maxfiles.plist
    ```

    You might have to `unload` the service before loading it.

## Download

Download the YugabyteDB `tar.gz` file using the following `wget` command.

```sh
$ wget https://downloads.yugabyte.com/yugabyte-2.0.1.0-darwin.tar.gz
```

To unpack the archive file and change to the YugabyteDB home directory, run the following command.

```sh
$ tar xvfz yugabyte-2.0.1.0-darwin.tar.gz && cd yugabyte-2.0.1.0/
```

## Configure

Add the following loopback IP addresses for use in the add node scenarios of the [Explore core features](../../explore/) section.

```sh
sudo ifconfig lo0 alias 127.0.0.2
sudo ifconfig lo0 alias 127.0.0.3
sudo ifconfig lo0 alias 127.0.0.4
sudo ifconfig lo0 alias 127.0.0.5
sudo ifconfig lo0 alias 127.0.0.6
sudo ifconfig lo0 alias 127.0.0.7
```
