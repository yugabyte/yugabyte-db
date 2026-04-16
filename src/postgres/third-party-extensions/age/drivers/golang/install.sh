#!/bin/sh

# get OS
os=$(uname)

# get architecture
arch=$(uname -m)

# Check Java installation
echo "Checking for minimum Java installation..."

java_version=$(java -version 2>&1 | awk -F '"' '/version/ {print $2}')

if [ ! -z $java_version ]; then
    echo "Java version $java_version"
else
    echo "Java not found. Please install Java."
fi

# Get Java version
java_major=$(echo $java_version | cut -d '.' -f1)
java_minor=$(echo $java_version | cut -d '.' -f2)
java_patch=$(echo $java_version | cut -d '.' -f3)

# Check Java version
java_version_flag=0
if [ $java_major -lt 11 ]; then
    java_version_flag=0
elif [ $java_minor -lt 0 ]; then
    java_version_flag=0
elif [ $java_patch -lt 0 ]; then
    java_version_flag=0
else
    java_version_flag=1
fi

if [ $java_version_flag -eq 0 ]; then
    echo "Java version less than 11.0.0"
    echo "NOTE: If a newer version of Java is installed, but not set as "
    echo "      the current version, exit and select it before continuing."
    exit 0
else
    echo "Java is installed."
    echo ""
fi

# Check ANTLR installation
echo "Checking for minimum ANTLR installation..."
jar xf /usr/local/antlr/antlr-*-complete.jar META-INF/MANIFEST.MF >/dev/null 2>&1
antlr_version=$(grep 'Implementation-Version' META-INF/MANIFEST.MF | cut -d ' ' -f 2)
rm -rf META-INF

if [ ! -z $antlr_version ]; then
    echo "ANTLR version $antlr_version"
else
    echo "ANTLR not found. Please install ANTLR."
    exit 0
fi

# Check ANTLR version
antlr_version_flag=0
antlr_major=$(echo $antlr_version | cut -d '.' -f 1)
antlr_minor=$(echo $antlr_version | cut -d '.' -f 2)
antlr_patch=$(echo $antlr_version | cut -d '.' -f 3 | sed 's/\r//')

if [ $antlr_major -lt 4 ]; then
    antlr_version_flag=0
elif [ $antlr_minor -lt 11 ]; then
    antlr_version_flag=0
elif [ $antlr_patch -lt 1 ]; then
    antlr_version_flag=0
else
    antlr_version_flag=1
fi

if [ $antlr_version_flag -eq 0 ]; then
    echo "ANTLR version less than 4.11.1"
    exit 0
else
    echo "ANTLR is installed."
    echo ""
fi

# Check Go installation
echo "Checking for minimum Golang installation..."
go_version=$(go version 2>&1 | cut -d' ' -f3)

if [ ! -z $go_version ]; then
    echo "Golang version $go_version"
else
    echo "Golang not found. Please install Golang."
    exit 0
fi

# Check Go version
go_version_flag=0
go_major=$(echo $go_version | cut -d '.' -f1 | cut -d 'o' -f 2)
go_minor=$(echo $go_version | cut -d '.' -f2)
go_patch=$(echo $go_version | cut -d '.' -f3)

if [ $go_major -lt 1 ]; then
    go_version_flag=0
elif [ $go_minor -lt 18 ]; then
    go_version_flag=0
elif [ $go_patch -lt 0 ]; then
    go_version_flag=0
else
    go_version_flag=1
fi

if [ $go_version_flag -eq 0 ]; then
    echo "Golang version less than 1.19.0"
    exit 0
else
    echo "Golang is installed."
    echo ""
fi

# Check CLASSPATH
echo "Checking for ANTLR in CLASSPATH..."
test_classpath=$(echo $CLASSPATH | grep antlr)

if [ ! -z $test_classpath ]; then
    echo "CLASSPATH = $CLASSPATH"
    echo ""
else
    echo "ANTLR not set in CLASSPATH. Please set up CLASSPATH."
    exit 0
fi

# Generate Parser and Lexer
echo "Generating Parser & Lexer..."
java -Xmx500M org.antlr.v4.Tool -Dlanguage=Go -visitor parser/Age.g4

# Install Golang driver
echo "Installing Driver..."
go get -u ./...

echo "Successfully Installed Driver!"
exit 0
