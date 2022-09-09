package main

var yumList = []string{"RedHat", "CentOS", "Oracle", "Alma", "Amazon"}

var aptList = []string{"Ubuntu", "Debian"}

// DetectOS detects the operating system yba-installer is running on.
func DetectOS() string {

	command1 := "bash"
	args1 := []string{"-c", "awk -F= '/^NAME/{print $2}' /etc/os-release"}
	output, _ := ExecuteBashCommand(command1, args1)

	return string(output)
}

//InstallOS performing OS specific installs (used for Python3)
func InstallOS(args []string) {

	var operatingSystem = DetectOS()

	if containsSubstring(yumList, operatingSystem) {

		argsFull := append([]string{"-y", "install"}, args...)
		ExecuteBashCommand("yum", argsFull)

	} else if containsSubstring(aptList, operatingSystem) {

		argsFull := append([]string{"-y", "install"}, args...)
		ExecuteBashCommand("apt-get", argsFull)

	}

}
