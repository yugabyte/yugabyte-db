@echo off

rem Install Java Development Kit (JDK)
echo Installing JDK...
if not exist "%ProgramFiles%\Java\jdk-19\" (
  mkdir C:\temp
  cd C:\temp
  powershell Invoke-WebRequest -Uri "https://download.oracle.com/java/19/archive/jdk-19.0.2_windows-x64_bin.exe" -OutFile jdk-19.0.2_windows-x64_bin.exe
  start /wait jdk-19.0.2_windows-x64_bin.exe /s ADDLOCAL="ToolsFeature" /s
  del /f jdk-19.0.2_windows-x64_bin.exe
  setx /M JAVA_HOME "C:\Program Files\Java\jdk-19.0.2"
  setx /M PATH "%PATH%;%JAVA_HOME%\bin"
) else (
  echo JDK already installed.
)

rem Install Apache Maven
echo Installing Apache Maven...
if not exist "%ProgramFiles%\Apache Maven\apache-maven-3.9.0\" (
  mkdir C:\temp
  cd C:\temp
  powershell Invoke-WebRequest -Uri "https://dlcdn.apache.org/maven/maven-3/3.9.0/binaries/apache-maven-3.9.0-bin.zip" -OutFile apache-maven-3.9.0-bin.zip
  powershell Expand-Archive apache-maven-3.9.0-bin.zip -DestinationPath \"%ProgramFiles%\Apache Maven\"
  del /f apache-maven-3.9.0-bin.zip
  setx /M PATH "%PATH%;%ProgramFiles%\Apache Maven\apache-maven-3.9.0-bin\bin"
) else (
  echo Apache Maven already installed.
)

rem Download and install ANTLR
echo Downloading ANTLR...
if not exist "%ProgramFiles%\ANTLR" (
  mkdir "%ProgramFiles%\ANTLR"
  cd "%ProgramFiles%\ANTLR"
  powershell Invoke-WebRequest -Uri "https://www.antlr.org/download/antlr-4.11.1-complete.jar" -OutFile "antlr-4.11.1-complete.jar"
  setx /M PATH "%PATH%;%ProgramFiles%\ANTLR\"
  setx /M CLASSPATH ".;%ProgramFiles%\ANTLR\antlr-4.11.1-complete.jar;%CLASSPATH%"
)

echo ANTLR installation complete.

rem Checking Compatablity for Golang
echo Checking if current version of Golang >= Go 1.18.....
set "minimum_version=1.18"
set "installed_version="
set "download_url=https://go.dev/dl/go1.19.7.windows-amd64.msi"

:: Check if Go is installed and get its version
set "go_path="
for %%i in (go.exe) do set "go_path=%%~$PATH:i"
if defined go_path (
	for /f "tokens=3" %%v in ('go version 2^>^&1') do set "installed_version=%%v"
)

:: If Go is not installed or the version is less than 1.18, prompt the user to install a new version
if not defined installed_version (
	echo installing Go
	:: Download and install the latest version of Go
	powershell -Command "& {Invoke-WebRequest -Uri "%download_url%" -OutFile '%TEMP%\go-minimum-version.msi'}"
	start /wait msiexec /i "%TEMP%\go-minimum-version.msi"
	for /f "tokens=3" %%v in ('go version 2^>^&1') do set "installed_version=%%v"
) else if "%installed_version%" lss "%minimum_version%" (
    set /p "install_new_version=Go version %minimum_version% or higher is required. Would you like to install the latest version? (y/n)"
    if /i "%install_new_version%"=="y" (
        :: Download and install the latest version of Go
        powershell -Command "& {Invoke-WebRequest -Uri "%download_url%" -OutFile '%TEMP%\go-minimum-version.msi'}"
        start /wait msiexec /i "%TEMP%\go-minimum_version.msi"
        for /f "tokens=3" %%v in ('go version 2^>^&1') do set "installed_version=%%v"
    ) else (
        echo Please update Go version before installing driver.
        goto skip
    )
)

rem Installing Driver
echo --^> Generating ANTLR parser ^& lexer ^for Golang%
java org.antlr.v4.Tool -Dlanguage=Go -visitor Age.g4 -o parser/
echo --^> Installing Driver
go get -u ./...
goto end
:skip
echo Aborted
:end
pause
endlocal