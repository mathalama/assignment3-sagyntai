#!/bin/sh
# Writer script for assignment 1
# Author: Gemini AI

# Check if both arguments are provided
if [ $# -lt 2 ]; then
    echo "Error: Two arguments required."
    echo "Usage: $0 <writefile> <writestr>"
    exit 1
fi

writefile=$1
writestr=$2

# Create the directory path if it does not exist
dirpath=$(dirname "$writefile")
if ! mkdir -p "$dirpath"; then
    echo "Error: Could not create directory $dirpath"
    exit 1
fi

# Create/overwrite the file with the specified string
# If file creation fails, exit with return code 1
if ! echo "$writestr" > "$writefile"; then
    echo "Error: Could not create or write to file $writefile"
    exit 1
fi

exit 0