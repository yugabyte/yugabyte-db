#!/bin/sh

os=$(uname)
arch=$(uname -m)

java=$(java -version 2>&1 | head -n 1 | cut -d ' ' -f 3 | cut -d '.' -f 1 | cut -d '"' -f 2)
# Check JDK version
echo "Checking for JDK..."
if ! java -version >/dev/null 2>&1 || [ $java -lt 11 ]; then
  echo "JDK not found or less than version 11, would you like to install? ()"
  echo "Y/N ->"
  read answer
  if [ "$answer" = y ] || [ "$answer" = Y ]; then
  	echo "Installing..."
		if [[ "$os" == "Darwin" ]]; then
			cd /tmp
		  if [[ "$arch" == "x86_64" ]]; then
		    curl "https://download.oracle.com/java/20/latest/jdk-20_macos-x64_bin.dmg" -o jdk-20_bin.dmg
		  elif [[ "$arch" == "arm64" ]]; then
		    curl "https://download.oracle.com/java/20/latest/jdk-20_macos-aarch64_bin.dmg" -o jdk-20_bin.dmg
		  fi
		  hdiutil mount jdk-20_bin.dmg
		  # sudo installer -pkg "/Volumes/JDK 20/JDK 20.pkg" -target "/"
		  sudo open -w "/Volumes/JDK 20/JDK 20.pkg"
		  hdiutil unmount "/Volumes/JDK 20"
		  rm jdk-20_bin.dmg

		elif [[ "$os" == "Linux" ]]; then
		  mkdir -p ~/tmp/jdk20
		  cd ~/tmp/jdk20
		  if [[ "$arch" == "x86_64" ]]; then
		  	curl "https://download.oracle.com/java/20/latest/jdk-20_linux-x64_bin.tar.gz" -o jdk-20_bin.tar.gz
		  elif [[ "$arch" == "arm64" ]]; then
		  	curl "https://download.oracle.com/java/20/latest/jdk-20_linux-aarch64_bin.tar.gz" -o jdk-20_bin.tar.gz
		  fi
		  sudo tar zxvf jdk-20_bin.tar.gz -C /usr/local/
		  cd ~/
		  rm -rf ~/tmp/jdk20/
  	fi
  	echo "JDK installation complete."
  else
  	echo "Please install JDK >= 11.0.16"
  	exit 0
	fi
else
  echo "JDK already installed."
fi

antlr=$(/usr/local/jdk-20/bin/jar xf /usr/local/antlr/antlr-*-complete.jar META-INF/MANIFEST.MF >/dev/null 2>&1 && grep 'Implementation-Version' META-INF/MANIFEST.MF | cut -d ' ' -f 2 && rm -rf META-INF)
# Check ANTLR installation and version
echo "Checking for ANTLR..."
check_antlr	() {
	if [ ! -z $antlr ]; then
		if ([ "$(echo $antlr | cut -d '.' -f 1)" -lt 4 ] && [ "$(echo $antlr | cut -d '.' -f 2)" -lt 11 ] && [ "$(echo $antlr | cut -d '.' -f 3)" -lt 1 ]); then
			return 0
		else
			return 1	
		fi
	else
		return 0
	fi
}

if check_antlr; then
	echo "ANTLR not found in Default Location or less than version 4.11.1, would you like to Install?"
	echo "(If installed in other location, please edit the location inside the shell script before running)"
	echo "Y/N ->"
	read answer
	if [ "$answer" = y ] || [ "$answer" = Y ]; then
			mkdir -p ~/tmp/antlr4.11.1
			cd ~/tmp/antlr4.11.1
			curl -LO "https://www.antlr.org/download/antlr-4.11.1-complete.jar"
			sudo mkdir -p /usr/local/antlr
			sudo mv ~/tmp/antlr4.11.1/antlr-4.11.1-complete.jar /usr/local/antlr/antlr-4.11.1-complete.jar

			echo 'export CLASSPATH=".:/usr/local/antlr/antlr-4.11.1-complete.jar"' >>~/.bashrc
			. ~/.bashrc
			echo "ANTLR installation complete."
		else
			echo "Please install ANTLR >= 4.11.1"
		exit 0
	fi
	#
else
	echo "ANTLR already installed."
fi

# Check Go installation and version
echo "Checking for Go..."
if ! command -v go >/dev/null 2>&1 || { [ $(go version | grep -o -E '[0-9]+\.[0-9]+' | head -n 1 | cut -d '.' -f 1) -lt 1 ] && [ $(go version | grep -o -E '[0-9]+\.[0-9]+' | head -n 1 | cut -d '.' -f 2) -lt 19 ]; }; then
	echo "Go not installed or version is less than 1.19, would you like to install?"
  echo "Y/N ->"
	read answer
  if [ "$answer" = y ] || [ "$answer" = Y ]; then	
		if [[ "$os" == "Darwin" ]]; then
			cd /tmp
		  if [[ "$arch" == "x86_64" ]]; then
		    curl "https://go.dev/dl/go1.20.2.darwin-amd64.pkg" -o go1.20.2.pkg
		  elif [[ "$arch" == "arm64" ]]; then
		    curl "https://go.dev/dl/go1.20.2.darwin-arm64.pkg" -o go1.20.2.pkg
		  fi
		  
	 		#sudo installer -pkg "go1.20.2.pkg" -target "/"
	 		sudo open -w golang.pkg
		  
	  elif [[ "$os" == "Linux" ]]; then
		  mkdir -p ~/tmp/go1.20.2
		  cd ~/tmp/go1.20.2
		  if [[ "$arch" == "x86_64" ]]; then
		  	curl "https://go.dev/dl/go1.20.2.linux-amd64.tar.gz" -o go1.20.2.tar.gz
		  elif [[ "$arch" == "arm64" ]]; then
		  	curl "https://go.dev/dl/go1.20.2.linux-arm64.tar.gz" -o go1.20.2.tar.gz
	  	fi
	  	
	  	if command -v go >/dev/null 2>&1; then
	  		rm -rf /usr/local/go
	  	fi
	  	sudo tar -C /usr/local -xzf go1.20.2.linux-amd64.tar.gz
	  	
	  	if ! [[ ":$PATH:" == *":/usr/local/go/bin:"* ]]; then
	  		export PATH=$PATH:/usr/local/go/bin
	  	fi
	  fi
	  echo "Go installation complete"
	else
  	echo "Please install Go >= 1.19"
  	exit 0
	fi
else
  echo "Go already installed."
fi



echo "Generating Parser & Lexer..."
java -Xmx500M -cp "/usr/local/lib/antlr-4.11.1-complete.jar:$CLASSPATH" org.antlr.v4.Tool -Dlanguage=Go -visitor Age.g4

echo "Installing Driver..."
go get -u ./...
echo "Successfully Installed Driver!"
exit 0
