#!/bin/sh
# Finder script for assignment 1
# Author: Aikyn Sagyntai

# Check if both arguments are provided
if [ $# -lt 2 ]; then
    echo "Error: Two arguments required."
    echo "Usage: $0 <filesdir> <searchstr>"
    exit 1
fi

filesdir=$1
searchstr=$2

# Check if filesdir is a valid directory
if [ ! -d "$filesdir" ]; then
    echo "Error: '$filesdir' is not a directory on the filesystem."
    exit 1
fi

# X: Count the number of files in the directory and subdirectories
# find lists files, wc -l counts the lines
X=$(find "$filesdir" -type f | wc -l)

# Y: Count the number of matching lines containing searchstr
# grep -r searches recursively, -o would match words, but here we count lines
Y=$(grep -r "$searchstr" "$filesdir" | wc -l)

echo "The number of files are $X and the number of matching lines are $Y"