#!/bin/sh
#By Abel Thomas

filesdir=$1
searchstr=$2

#check for two arguments
if [ $# -eq 2 ]
then

	#check that dir exists
	if [ ! -d "$filesdir" ]
	then
		exit 1
	fi

	#https://stackoverflow.com/questions/5905054/how-can-i-recursively-find-all-files-in-current-#and-subfolders-based-on-wildcard
	#find all directories and subdirectories 

	numfiles=$(find "$filesdir" -type f | wc -l)


	#https://stackoverflow.com/questions/16956810/find-all-files-containing-a-specific-text-string-#on-linux
	#find all occurences of string in directories and subdirectories

	numstring=$(grep -rw "${searchstr}" "${filesdir}" | wc -l)

	echo "The number of files are ${numfiles} and the number of matching lines are ${numstring}"

	exit 0
	
else
	echo "Two arguments required <filesdir> <searchstr>"	
	exit 1
fi

