#!/bin/bash
#By Abel Thomas

writefile=$1
writestr=$2

#trigger actions runner

#check for two arguments
if [ $# -eq 2 ] 
then
	#check that dir exists
	if [ ! -d "${writefile}" ]
	then
	
		#if not create file	
		mkdir -p "$(dirname $writefile)"
	fi
	
	#put in contents
	echo "${writestr}" > "${writefile}"
	exit 0
else 
	echo "Two arguments required <writefile> <writestr>"	
	exit 1	
fi
