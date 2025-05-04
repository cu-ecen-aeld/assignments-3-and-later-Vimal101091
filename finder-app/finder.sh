#!/bin/bash

filesdir="$1"
searchstr="$2"

# Check if both parameters are provided
if [ -z "$filesdir" ] || [ -z "$searchstr" ]; then
    echo "Error: Missing parameters."
    exit 1

elif [ ! -d "$filesdir" ]; then
    echo "Error: Directory '$filesdir' does not exist."
    exit 1

else
    # Count the number of files (X)
    file_count=$(find "$filesdir" -type f | wc -l)

    # Count matching lines (Y)
    match_count=$(grep -r "$searchstr" "$filesdir" | wc -l)

    # Output the result
    echo "The number of files are $file_count and the number of matching lines are $match_count"
fi
