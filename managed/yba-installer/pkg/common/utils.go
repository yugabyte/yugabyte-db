/*
* Copyright (c) YugaByte, Inc.
 */

package common

import (
	"bufio"
	"crypto/rand"
	"database/sql"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"

	"github.com/spf13/viper"
	"github.com/vmware-labs/yaml-jsonpath/pkg/yamlpath"
	"gopkg.in/yaml.v3"

	"github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/common/shell"
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
	// "github.com/yugabyte/yugabyte-db/managed/yba-installer/preflight"
)

// Hardcoded Variables.

// Systemctl linux command.
const Systemctl string = "systemctl"

const PostgresPackageGlob = "yba_installer-*linux*/postgres-linux-*.tar.gz"

const ybdbPackageGlob = "yba_installer-*linux*/yugabyte-*-linux-x86_64.tar.gz"

var skipConfirmation = false

var yumList = []string{"RedHat", "CentOS", "Oracle", "Alma", "Amazon"}

var aptList = []string{"Ubuntu", "Debian"}

const GoBinaryName = "yba-ctl"

const VersionMetadataJSON = "version_metadata.json"

const javaBinaryGlob = "yba_installer-*linux*/OpenJDK17U-jre_x64_linux_*.tar.gz"

const tarTemplateDirGlob = "yba_installer-*linux*/" + ConfigDir

const tarCronDirGlob = "yba_installer-*linux*/" + CronDir

// IndexOf returns the index in arr where val is present, -1 otherwise.
func IndexOf(arr []string, val string) int {

	for pos, v := range arr {
		if v == val {
			return pos
		}
	}

	return -1
}

// Contains checks an array values for the presence of target.
// Type must be a comparable
func Contains[T comparable](values []T, target T) bool {
	for _, v := range values {
		if v == target {
			return true
		}
	}
	return false
}

// Chown changes ownership of dir to user:group, recursively (optional).
func Chown(dir, user, group string, recursive bool) error {
	log.Debug(fmt.Sprintf("Chown of dir %s to user %s, recursive=%t",
		dir, user, recursive))
	args := []string{fmt.Sprintf("%s:%s", user, group), dir}
	if recursive {
		args = append([]string{"-R"}, args...)
	}

	out := shell.Run("chown", args...)
	out.SucceededOrLog()
	return out.Error
}

// HasSudoAccess determines whether or not running user has sudo permissions.
func HasSudoAccess() bool {

	cmd := exec.Command("id", "-u")
	output, err := cmd.Output()
	if err != nil {
		log.Fatal("Error: " + err.Error() + ".")
	}

	i, err := strconv.Atoi(string(output[:len(output)-1]))
	if err != nil {
		log.Fatal("Error: " + err.Error() + ".")
	}

	if i == 0 {
		return true
	}
	return false
}

// SplitInput parses an input string that is either comma or space separated and returns the
// individual elements in an array
func SplitInput(input string) []string {
	return strings.FieldsFunc(input, func(r rune) bool {
			return r == ',' || r == ' '
	})
}

// Create or truncate a file at a relative path for the non-root case. Have to make the directory
// before inserting the file in that directory.
func Create(p string) (*os.File, error) {
	log.Debug("creating file (and parent directories) " + p)
	if err := MkdirAll(filepath.Dir(p), DirMode); err != nil {
		return nil, err
	}
	return os.Create(p)
}

// CreateSymlink of binary from pkgDir to linkDir.
/*
	pkgDir - directory where the binary (file or directory) is located
	linkDir - directory where you want the link to be created
	binary - name of file or directory to link. should already exist in pkgDir and will be the same
*/
func CreateSymlink(pkgDir string, linkDir string, binary string) error {
	binaryPath := fmt.Sprintf("%s/%s", pkgDir, binary)
	linkPath := fmt.Sprintf("%s/%s", linkDir, binary)

	args := []string{"-sf", binaryPath, linkPath}
	log.Debug(fmt.Sprintf("Creating symlink at %s -> orig %s",
		linkPath, binaryPath))

	out := shell.Run("ln", args...)
	out.SucceededOrLog()
	return out.Error
}

// Symlink implements a more generic symlink utility.
func Symlink(src string, dest string) error {
	out := shell.Run("ln", "-sf", src, dest)
	out.SucceededOrLog()
	return out.Error
}

func ResolveSymlink(source, target string) error {
	_, tErr := os.Stat(target)
	_, sErr := os.Stat(source)
	// Handle non errNotExist errors
	if tErr != nil && !errors.Is(tErr, fs.ErrNotExist) {
		return fmt.Errorf("failed to stat target %s: %w", target, tErr)
	}
	if sErr != nil && !errors.Is(sErr, fs.ErrNotExist) {
		return fmt.Errorf("failed to stat source %s: %w", target, sErr)
	}

	// Handle neither source nor target existing
	if errors.Is(tErr, fs.ErrNotExist) && errors.Is(sErr, fs.ErrNotExist) {
		msg := fmt.Sprintf("Neither source %s nor target %s exist", source, target)
		log.Error(msg)
		return fmt.Errorf(msg)
		// Handle only target existing (already resolved)
	} else if tErr == nil && errors.Is(sErr, fs.ErrNotExist) {
		log.Debug(fmt.Sprintf("Symlink %s -> %s already resolved", source, target))
		return nil
	}

	// Remove the target if needed
	if tErr == nil {
		if err := os.RemoveAll(target); err != nil {
			log.Error("failed to delete link target " + target)
			return fmt.Errorf("failed to remove target %s: %w", target, err)
		}
	}
	// Try to do an os.Rename. This will fail across filesystems
	if err := os.Rename(source, target); err != nil {
		if strings.Contains(err.Error(), "invalid cross-device link") {
			log.Debug("cross-device link detected, using fallback copy implementation")
			return resolveSymlinkFallback(source, target)
		}
		log.Error(fmt.Sprintf("failed to rename %s -> %s", source, target))
		return fmt.Errorf("resolve symlink failed to rename %s->%s: %w", source, target, err)
	}
	return nil
}

// resolveSymlinkFallback will, given a source (file/dir) and target (symlink), it will move the source to
// the target destination. This supports moving across filesystems/devices, and is idempotent.
// Logic to be idempotent:
// 1. if the source directory exists always copy it to the target
// 1.A delete the target before copy if needed
// 2. move the source to source-tmp
// 3. Best effort to delete source-tmp.
// Fail if neither source nor target exist
func resolveSymlinkFallback(source, target string) error {
	srcTmpName := fmt.Sprintf("%s-tmp", source)

	// Source still exists, copy to target
	if _, sErr := os.Stat(source); sErr == nil {
		// Delete Target if it exists
		if _, tErr := os.Stat(target); tErr == nil {
			if err := RemoveAll(target); err != nil {
				return fmt.Errorf("target directory %s could not be deleted: %w", target, err)
			}
		} else if errors.Is(tErr, fs.ErrNotExist) {
			log.DebugLF("target directory already deleted")
		} else {
			return fmt.Errorf("could not determine status of target directory %s: %w", target, tErr)
		}
		log.Debug("Copy symlink source to target")
		if out := shell.Run("cp", "-r", source, target); !out.SucceededOrLog() {
			return fmt.Errorf("copying from %s -> %s failed while resolving symlink: %w",
				source, target, out.Error)
		}

		log.DebugLF("Rename source to temp name")
		if err := os.Rename(source, srcTmpName); err != nil {
			return fmt.Errorf("rename to temp dir: %w", err)
		}
	} else if errors.Is(sErr, fs.ErrNotExist) {
		log.DebugLF("no source directory found")
		if _, tErr := os.Stat(target); errors.Is(tErr, fs.ErrNotExist) {
			log.Error("no source or target found")
			return fmt.Errorf("could not find source %s nor target %s directories", source, target)
		}
	} else {
		return fmt.Errorf("could not determine status of source directory %s: %w", source, sErr)
	}

	// Remove the temp dir if it exists. This is best effort, and can be cleaned up manually later
	if err := RemoveAll(srcTmpName); err != nil {
		log.Warn("failed to remove backup source directory " + srcTmpName + ": " + err.Error())
	}
	return nil
}

// Copy will copy the source to the destination
/*
	src - source file or directory
	dest - destination file or directory
	recursive - if it should be a recursive copy
	preserve  - preserve file stats
*/
func Copy(src, dest string, recursive, preserve bool) error {
	args := []string{}
	if recursive {
		args = append(args, "-r")
	}
	if preserve {
		args = append(args, "-p")
	}
	args = append(args, src, dest)
	out := shell.Run("cp", args...)
	out.SucceededOrLog()
	return out.Error
}

type defaultAnswer int

func (d defaultAnswer) String() string {
	return strconv.Itoa(int(d))
}

const (
	DefaultNone defaultAnswer = iota
	DefaultYes
	DefaultNo
)

// DisableUserConfirm skips all confirmation steps.
func DisableUserConfirm() {
	skipConfirmation = true
}

// UserConfirm asks the user for confirmation before proceeding.
// Returns true if the user is ok with proceeding (or if --force is spec'd)
func UserConfirm(prompt string, defAns defaultAnswer) bool {
	if skipConfirmation {
		return true
	}
	if !strings.HasSuffix(prompt, " ") {
		prompt = prompt + " "
	}
	var selector string
	switch defAns {
	case DefaultNone:
		selector = "[yes/no]"
	case DefaultYes:
		selector = "[YES/no]"
	case DefaultNo:
		selector = "[yes/NO]"
	default:
		log.Fatal("unknown defaultanswer " + defAns.String())
	}

	for {
		fmt.Printf("%s%s: ", prompt, selector)
		reader := bufio.NewReader(os.Stdin)
		input, err := reader.ReadString('\n')
		if err != nil {
			fmt.Println("invalid input: " + err.Error())
			continue
		}
		input = strings.TrimSuffix(input, "\n")
		input = strings.Trim(input, " ")
		input = strings.ToLower(input)
		switch input {
		case "n", "no":
			return false
		case "y", "yes":
			return true
		case "":
			if defAns == DefaultYes {
				return true
			} else if defAns == DefaultNo {
				return false
			}
			fallthrough
		default:
			fmt.Println("please enter 'yes' or 'no'")
		}
	}
}

// InitViper will set some sane defaults as needed, and read the config.
func InitViper() {
	// Init Viper
	viper.SetDefault("service_username", DefaultServiceUser)
	viper.SetDefault("installRoot", "/opt/yugabyte")

	// Update the installRoot to home directory for non-root installs. Will honor custom install root.
	if !HasSudoAccess() && viper.GetString("installRoot") == "/opt/yugabyte" {
		viper.SetDefault("installRoot", filepath.Join(GetUserHomeDir(), "yugabyte"))
	}
	viper.SetConfigFile(InputFile())
	viper.ReadInConfig()

	//Enable ybdb only when YBA_MODE = dev and USE_YBDB = 1 in the env.
	if !IsPostgresEnabled() && os.Getenv("YBA_MODE") == "dev" && os.Getenv("USE_YBDB") == "1" {
		//TODO: Move this control to yba-ctl.yml eventually.
		viper.Set("ybdb.install.enabled", true)
		viper.Set("ybdb.install.port", 5433)
		viper.Set("ybdb.install.restartSeconds", 10)
		viper.Set("ybdb.install.read_committed_isolation", true)
	}
}

// Checks if Postgres is enabled in config.
func IsPostgresEnabled() bool {
	return viper.GetBool("postgres.install.enabled") || viper.GetBool("postgres.useExisting.enabled")
}

func GetUserHomeDir() string {
	homeDir, err := os.UserHomeDir()
	if err != nil {
		panic(err)
	}
	return homeDir
}

func GetBinaryDir() string {

	exPath, err := os.Executable()
	if err != nil {
		log.Fatal("Error determining yba-ctl binary path.")
	}
	realPath, err := filepath.EvalSymlinks(exPath)
	if err != nil {
		log.Fatal("Error eval symlinks for yba-ctl binary path.")
	}
	return filepath.Dir(realPath)
}

type YBVersion struct {

	// ex: 2.17.1.0-b235-foo
	// major, minor, patch, subpatch in order
	// ex: 2.17.1.0
	PublicVersionDigits []int

	// ex: 235
	BuildNum int

	// ex: foo
	Remainder string
}

func NewYBVersion(versionString string) (*YBVersion, error) {
	version := &YBVersion{
		PublicVersionDigits: []int{-1, -1, -1, -1},
		BuildNum:            -1,
	}

	if versionString == "" {
		return version, nil
	}

	re := regexp.MustCompile(
		`^(?P<major>[0-9]+)\.(?P<minor>[0-9]+)\.(?P<patch>[0-9]+)\.(?P<subpatch>[0-9]+)(?:-b(?P<build>[0-9]+)[-a-z]*)?$`)
	matches := re.FindStringSubmatch(versionString)
	if matches == nil || len(matches) < 5 {
		return version, fmt.Errorf("invalid version string %s", versionString)
	}

	var err error
	version.PublicVersionDigits[0], err = strconv.Atoi(matches[1])
	if err != nil {
		return version, err
	}

	version.PublicVersionDigits[1], err = strconv.Atoi(matches[2])
	if err != nil {
		return version, err
	}

	version.PublicVersionDigits[2], err = strconv.Atoi(matches[3])
	if err != nil {
		return version, err
	}

	version.PublicVersionDigits[3], err = strconv.Atoi(matches[4])
	if err != nil {
		return version, err
	}

	if len(matches) > 5 && matches[5] != "" {
		version.BuildNum, err = strconv.Atoi(matches[5])
		if err != nil {
			return version, err
		}
	}

	if len(matches) > 6 && matches[6] != "" {
		version.Remainder = matches[6]
	}

	return version, nil

}

func (ybv YBVersion) String() string {
	reprStr, _ := json.Marshal(ybv)
	return string(reprStr)
}

func IsValidVersion(fullVersion string) bool {
	_, err := NewYBVersion(fullVersion)
	return err == nil
}

// returns true if version1 < version2
func LessVersions(version1, version2 string) bool {
	ybversion1, err := NewYBVersion(version1)
	if err != nil {
		panic(err)
	}

	ybversion2, err := NewYBVersion(version2)
	if err != nil {
		panic(err)
	}

	for i := 0; i < 4; i++ {
		if ybversion1.PublicVersionDigits[i] != ybversion2.PublicVersionDigits[i] {
			return ybversion1.PublicVersionDigits[i] < ybversion2.PublicVersionDigits[i]
		}
	}
	if ybversion1.BuildNum != ybversion2.BuildNum {
		return ybversion1.BuildNum < ybversion2.BuildNum
	}

	return ybversion1.Remainder < ybversion2.Remainder
}

// only keep elements which eval to true on filter func
func FilterList[T any](sourceList []T, filterFunc func(T) bool) (result []T) {
	for _, s := range sourceList {
		if filterFunc(s) {
			result = append(result, s)
		}
	}
	return
}

func GetJsonRepr[T any](obj T) []byte {
	reprStr, _ := json.Marshal(obj)
	return reprStr
}

func init() {
	InitViper()
}

// UpdateRootInstall will update the yaml files .installRoot entry with what is currently
// set in viper.
func UpdateRootInstall(newRoot string) {
	viper.Set("installRoot", newRoot)
	SetYamlValue(InputFile(), "installRoot", newRoot)
}

// assumes path is '.' separated list of keys where to place value
// creates a mapping entry for every key in path except for the last key which maps to value
// value can be of type string/int/bool
func createYamlValue(root *yaml.Node, path string, value interface{}) error {
	if root.Kind == yaml.DocumentNode {
		return createYamlValue(root.Content[0], path, value)
	}
	var key, val yaml.Node
	if i := strings.Index(path, "."); i > 0 {
		// Need to create a map entry for all the parts up until the entry beyond last period (base)
		val = yaml.Node{Kind: yaml.MappingNode, Tag: "!!map"}
		key.SetString(path[:i])
		root.Content = append(root.Content, &key, &val)
		createYamlValue(&val, path[i+1:], value)
	} else {
		// only scalar part
		key.SetString(path)
		if err := setValue(&val, value); err != nil {
			return err
		}
		root.Content = append(root.Content, &key, &val)
	}
	return nil
}

// Recursive helper function that does the work of looking for the path and determining how to
// insert the new value into the tree (either via addition, replacement, or creation of new nodes)
func setYamlValue(root *yaml.Node, findPath, setPath string, value interface{}) error {
	yPath, err := yamlpath.NewPath(findPath)
	if err != nil {
		log.Error(fmt.Sprintf("malformed yaml path %s", findPath))
		return err
	}
	matchNodes, err := yPath.Find(root)
	if strings.TrimSpace(setPath) == "" {
		// base case where we actually need to set the value
		if len(matchNodes) != 1 {
			if root.Kind == yaml.DocumentNode {
				// Sometimes we may be setting directly on the Document in which case we need to create
				// a new entry if the path is not found
				if err = createYamlValue(root, findPath, value); err != nil {
					return err
				}
				return nil
			}
			// Only scalar part does not exist, create two nodes and append to Content
			var key, val yaml.Node
			key.SetString(findPath)
			if err := setValue(&val, value); err != nil {
				return err
			}
			root.Content = append(root.Content, &key, &val)
		} else {
			// Path already existed in yaml, need to override
			if err := setValue(matchNodes[0], value); err != nil {
				return err
			}
		}
	} else { // recursive case where there may be more levels to create/follow before setting value
		if len(matchNodes) != 1 {
			// Path does not exist in yaml, need to create all entries (including findPath)
			if err = createYamlValue(root, fmt.Sprintf("%s.%s", findPath, setPath), value); err != nil {
				return err
			}
			return nil
		}
		// Path exists in yaml, recursively follow remaining path to set value
		before, after, _ := strings.Cut(setPath, ".")
		return setYamlValue(matchNodes[0], before, after, value)
	}
	return nil
}

// Function to parse an arbitrary value and set the correct tag in a yaml Node
func setValue(node *yaml.Node, value interface{}) error {
	node.Kind = yaml.ScalarNode
	node.Style = 0
	switch t := value.(type) {
	case int:
		vint := value.(int)
		node.Tag = "!!int"
		node.Value = strconv.Itoa(vint)
	case bool:
		vbool := value.(bool)
		node.Tag = "!!bool"
		node.Value = strconv.FormatBool(vbool)
	case string:
		vstring := value.(string)
		node.SetString(vstring)
	case []string:
		node.Style = yaml.FlowStyle
		node.Kind = yaml.SequenceNode
		// Check if the slice is empty, if not, populate the content
		if len(value.([]string)) > 0 {
			var content []*yaml.Node
			for _, v := range value.([]string) {
				elem := &yaml.Node{Kind: yaml.ScalarNode, Value: string(v)}
				content = append(content, elem)
			}
			node.Content = content
		}
	default:
		return fmt.Errorf("unexpected type %T", t)
	}
	return nil
}

// Helper function used during dev debugging.
func logNode(node *yaml.Node) {
	log.Info(fmt.Sprintf("top level node: %#v", node))
	log.Info(fmt.Sprintf("printing children"))
	for _, np := range node.Content {
		logNode(np)
	}
	log.Info("end log node")
}

// SetYamlValue sets the entry yamlPath to value in filePath
func SetYamlValue(filePath string, yamlPath string, value interface{}) error {
	origYamlBytes, err := os.ReadFile(filePath)
	if err != nil {
		return fmt.Errorf("unable to read config file %s: %s", filePath, err.Error())
	}

	var root yaml.Node
	err = yaml.Unmarshal(origYamlBytes, &root)
	if err != nil {
		return fmt.Errorf("unable to parse config file %s: %s", filePath, err.Error())
	}

	before, after, _ := strings.Cut(yamlPath, ".")
	err = setYamlValue(&root, before, after, value)
	if err != nil {
		return fmt.Errorf("error seting yaml value: %s", err.Error())
	}

	finalYaml, err := yaml.Marshal(&root)
	if err != nil {
		return fmt.Errorf("error serializing yaml: %s", err.Error())
	}
	err = os.WriteFile(filePath, finalYaml, 0600)
	if err != nil {
		return fmt.Errorf("error writing to file %s: %s", filePath, err.Error())
	}
	return nil
}

func generateRandomBytes(n int) ([]byte, error) {

	b := make([]byte, n)
	_, err := rand.Read(b)
	if err != nil {
		return nil, err
	}
	return b, nil
}

// generateRandomStringURLSafe is used to generate random passwords.
func GenerateRandomStringURLSafe(n int) string {

	b, _ := generateRandomBytes(n)
	return base64.URLEncoding.EncodeToString(b)
}

func GuessPrimaryIP() string {
	conn, err := net.Dial("udp", "8.8.8.8:80")
	if err != nil {
		panic(err)
	}
	defer conn.Close()

	localAddr := conn.LocalAddr().(*net.UDPAddr)
	return localAddr.IP.String()
}

func GetFileMatchingGlob(glob string) (string, int, error) {
	matches, err := filepath.Glob(glob)
	if err != nil || len(matches) != 1 {
		return "", len(matches), fmt.Errorf(
			"Expect to find one match for glob %s (err %s, matches %v)",
			glob, err, matches)
	}
	return matches[0], 1, nil
}

func GetFileMatchingGlobOrFatal(glob string) string {
	result, _, err := GetFileMatchingGlob(glob)
	if err != nil {
		log.Fatal(err.Error())
	}
	return result
}

// given a path like /a/b/c/d that may not exist,
// returns the first valid directory in the hierarchy
// that actually exists
func GetValidParent(dir string) (string, error) {
	curDir := filepath.Clean(dir)
	_, curError := os.Stat(curDir)
	lastDir := ""
	for curError != nil && lastDir != curDir {
		lastDir = curDir
		curDir = filepath.Dir(curDir)
		_, curError = os.Stat(curDir)
	}
	return curDir, curError
}

func GetPostgresConnection(dbname string) (*sql.DB, string, error) {
	var user, host, port, pwd, connStr string
	if viper.GetBool("postgres.useExisting.enabled") {
		user = viper.GetString("postgres.useExisting.username")
		host = viper.GetString("postgres.useExisting.host")
		port = viper.GetString("postgres.useExisting.port")
		pwd = viper.GetString("postgres.useExisting.password")
	}
	if viper.GetBool("postgres.install.enabled") {
		user = "postgres"
		host = "localhost"
		port = viper.GetString("postgres.install.port")
	}
	nonPwdConnStr := fmt.Sprintf(
		"user='%s' host=%s port=%s dbname=%s sslmode=disable",
		user,
		host,
		port,
		dbname)
	log.Debug(fmt.Sprintf("Attempting to connect to db with conn str %s", nonPwdConnStr))
	// add pwd later so we don't log it above
	if pwd != "" {
		connStr = nonPwdConnStr + fmt.Sprintf(" password='%s'", pwd)
	} else {
		connStr = nonPwdConnStr
	}
	db, err := sql.Open("postgres" /*driverName*/, connStr)
	if err != nil {
		log.Debug(err.Error())
		return nil, nonPwdConnStr, err
	}
	return db, nonPwdConnStr, nil
}

// RunFromInstalled will return if yba-ctl is an "instanlled" yba-ctl or one locally executed
func RunFromInstalled() bool {
	path, err := os.Executable()
	if err != nil {
		log.Fatal("unable to determine yba-ctl executable path: " + err.Error())
	}

	matcher, err := regexp.Compile("(?:/opt/yba-ctl/yba-ctl)|(?:/usr/bin/yba-ctl)|" +
		"(?:.*/yugabyte/software/.*/yba_installer/yba-ctl)")

	if err != nil {
		log.Fatal("could not compile regex: " + err.Error())
	}
	return matcher.MatchString(path)
}

// Bool2Int converts a bool to int - 1 for true, 0 for false.
func Bool2Int(b bool) int {
	if b {
		return 1
	}
	return 0
}

// AbsoluteBundlePath returns the absolute path to the given file, assuming that file is a relative
// path from the extracted yba_installer_full tgz file. This is used for installs, allowing us to
// run yba-ctl as `./yba_installer_full-b123/yba-ctl install` and still find the binaries needed
// for installation or upgrade - basically any execution of yba-ctl from the full bundle.
func AbsoluteBundlePath(fp string) string {
	executable, err := os.Executable()
	if err != nil {
		log.Fatal(fmt.Sprintf("failed to determine executable path: %s", err.Error()))
	}
	rootDir := filepath.Dir(executable)
	return filepath.Join(rootDir, fp)
}
