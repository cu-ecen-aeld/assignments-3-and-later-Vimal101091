#!/bin/bash

writefile="$1"
writestr="$2"

# Check if both parameters are provided
if [ -z "$writefile" ] || [ -z "$writestr" ]; then
    echo "Error: Missing parameters."
    exit 1
else
    # Extract directory path
    dirpath=$(dirname "$writefile")

    # Create directory path if it doesn't exist
    mkdir -p "$dirpath"
    if [ $? -ne 0 ]; then
        echo "Error: Directory path '$dirpath' could not be created."
        exit 1
    fi

    # Attempt to write the string to the file
    echo "$writestr" > "$writefile"
    if [ $? -ne 0 ]; then
        echo "Error: File '$writefile' could not be written."
        exit 1
    fi
fi




