## Prerequisites

a) <i class="fab fa-apple" aria-hidden="true"></i> macOS 10.12 (Sierra) or higher

b) Verify that you have python2 installed. Support for python3 is in the works.

```sh
$ python --version
```

```
Python 2.7.10
```

c) Each tablet maps to its own file, so if you experiment with a few hundred tables and a few tablets per table, you can soon end up creating a large number of files in the current shell. Make sure that this command shows a big enough value.

```sh
$ launchctl limit maxfiles
```

We recommend simply setting the soft and hard limits to 1048576.

- Edit `/etc/sysctl.conf` with the following contents.

```sh
kern.maxfiles=1048576
kern.maxproc=2500
kern.maxprocperuid=2500
kern.maxfilesperproc=1048576
```

- If your macOS version does not have the `/etc/sysctl.conf` file, then ensure that the file `/Library/LaunchDaemons/limit.maxfiles.plist` has the following content.

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

Ensure that the plist file is owned by `root:wheel` and has permissions `-rw-r--r--`. Reboot your computer for this to take effect. Or, to avoid this effort, enter this command:

```sh
$ sudo launchctl load -w /Library/LaunchDaemons/limit.maxfiles.plist
```

You might have to `unload` the service before loading it.

## Download

Download the YugabyteDB tar.gz as shown below.

```sh
$ wget https://downloads.yugabyte.com/yugabyte-1.3.2.1-darwin.tar.gz
```

```sh
$ tar xvfz yugabyte-1.3.2.1-darwin.tar.gz && cd yugabyte-1.3.2.1/
```

## Configure

Add a few loopback IP addresses to cover the add node scenarios of the [Explore Core Features](../../explore/) section.

```sh
sudo ifconfig lo0 alias 127.0.0.2
sudo ifconfig lo0 alias 127.0.0.3
sudo ifconfig lo0 alias 127.0.0.4
sudo ifconfig lo0 alias 127.0.0.5
sudo ifconfig lo0 alias 127.0.0.6
sudo ifconfig lo0 alias 127.0.0.7
```
