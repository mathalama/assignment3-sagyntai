#!/bin/sh
# Tester script for assignment 1 and assignment 2
# Author: Siddhant Jajoo

set -e
set -u

NUMFILES=10
WRITESTR=AELD_IS_FUN
WRITEDIR=/tmp/aeld-data
# Assignment 4: conf dir logic
if [ -d "/etc/finder-app/conf" ]; then
    CONFDIR="/etc/finder-app/conf"
else
    CONFDIR="$(dirname "$0")/../conf"
fi
username=$(cat "${CONFDIR}/username.txt")

if [ $# -lt 3 ]
then
    echo "Using default value ${WRITESTR} for string to write"
    if [ $# -lt 1 ]
    then
        echo "Using default value ${NUMFILES} for number of files to write"
    else
        NUMFILES=$1
    fi  
else
    NUMFILES=$1
    WRITESTR=$2
    WRITEDIR=/tmp/aeld-data/$3
fi

MATCHSTR="The number of files are ${NUMFILES} and the number of matching lines are ${NUMFILES}"

# --- ИЗМЕНЕНИЯ ДЛЯ МОДУЛЯ 5 ---
# 1. Удаляем старые артефакты сборки и компилируем 'writer' нативно
# make -C "$(dirname "$0")" clean
# make -C "$(dirname "$0")"
# ------------------------------

echo "Writing ${NUMFILES} files containing string ${WRITESTR} to ${WRITEDIR}"

rm -rf "${WRITEDIR}"

assignment=$(cat "${CONFDIR}/assignment.txt")

if [ $assignment != 'assignment1' ]
then
    mkdir -p "$WRITEDIR"
    if [ -d "$WRITEDIR" ]
    then
        echo "$WRITEDIR created"
    else
        exit 1
    fi
fi

# Assignment 4: Path logic for writer
if [ -x "$(dirname "$0")/writer" ]; then
    WRITER_CMD="$(dirname "$0")/writer"
else
    WRITER_CMD="writer"
fi

# Assignment 4: Path logic for finder.sh
if [ -x "$(dirname "$0")/finder.sh" ]; then
    FINDER_CMD="$(dirname "$0")/finder.sh"
else
    FINDER_CMD="finder.sh"
fi

for i in $( seq 1 $NUMFILES)
do
    # 2. ЗАМЕНА: Используем утилиту './writer' вместо './writer.sh'
    "${WRITER_CMD}" "$WRITEDIR/${username}$i.txt" "$WRITESTR"
done

OUTPUTSTRING=$("${FINDER_CMD}" "$WRITEDIR" "$WRITESTR")

# remove temporary directories
rm -rf /tmp/aeld-data

# Assignment 4: Write output to file
echo "${OUTPUTSTRING}" > /tmp/assignment4-result.txt

set +e
echo ${OUTPUTSTRING} | grep "${MATCHSTR}"
if [ $? -eq 0 ]; then
    echo "success"
    exit 0
else
    echo "failed: expected  ${MATCHSTR} in ${OUTPUTSTRING} but instead found"
    exit 1
fi
