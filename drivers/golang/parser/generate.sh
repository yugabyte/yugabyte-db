#!/bin/sh
GRAMMAR_LOC="$(dirname $(pwd))/parser"

mkdir -p ~/tmp/antlr
cd ~/tmp/antlr
curl -LO "https://www.antlr.org/download/antlr-4.11.1-complete.jar"
echo "ANTLR installation complete."

echo "Generating Parser & Lexer..."
java -Xmx500M -cp "$HOME/tmp/antlr/antlr-4.11.1-complete.jar:$HOME/tmp/antlr/antlr-4.11.1-complete.jar" org.antlr.v4.Tool -Dlanguage=Go -visitor $GRAMMAR_LOC/Age.g4
exit 0

