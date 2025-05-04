#!/bin/bash

filesdir="$1"
searchstr="$2"

# Check if both parameters are provided
if [ -z "$filesdir" ] || [ -z "$searchstr" ]; then
    echo "Error: Missing parameters."
    echo "Usage: $0 <directory> <search_string>"
    exit 1

elif [ ! -d "$filesdir" ]; then
    echo "Error: Directory '$filesdir' does not exist."
    exit 1

else
    # Count the number of files (X)
    file_count=$(find "$filesdir" -type f | wc -l)

    # Count matching lines (Y)
    match_count=$(grep -r "$searchstr" "$filesdir" --binary-files=without-match 2>/dev/null | wc -l)

    # Output the result
    echo "The number of files are $file_count and the number of matching lines are $match_count"
fi
