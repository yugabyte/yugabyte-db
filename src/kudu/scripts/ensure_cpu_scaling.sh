#!/bin/bash -e
# Ensure that the CPU governor is set to a particular governor, outputting
# the prior governor on stdout.
#
# Without this, some of our tests end up having higher variance due to
# changing CPU speed during the test.
#
# Assumes that all CPUs are set to the same governor.
target_governor=$1
old_governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)
for cpu_dir in /sys/devices/system/cpu/cpu[0-9]*/ ; do
    governor_file=$cpu_dir/cpufreq/scaling_governor
    governor=$(cat $governor_file)
    if [ "$governor" != "$target_governor" ]; then
        >&2 echo "CPU $cpu_dir not in '$target_governor' mode. Attempting to change"
        echo $target_governor | sudo tee $governor_file > /dev/null
        if [ $? -ne 0 ]; then
            >&2 echo Could not set $target_governor governor!
            >&2 echo Perhaps you need passwordless sudo for this user
            exit 1
        fi
    fi
done
echo $old_governor
