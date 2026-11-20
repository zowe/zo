#!/usr/bin/env bash
set -e

echo "setting required env vars..."
export _BPXK_AUTOCVT=ON
export _CEE_RUNOPTS="$_CEE_RUNOPTS FILETAG(AUTOCVT,AUTOTAG) POSIX(ON)"
export _TAG_REDIR_ERR=txt
export _TAG_REDIR_IN=txt
export _TAG_REDIR_OUT=txt

user=$(whoami)
data_set=$user.test.temp.jcl
data_set_jcl="$data_set(iefbr14)"

testing="Testing"
passed="..passed!"
failed="..failed!"

if [ "$1" == "-h" ] || [ "$1" == "--help" ]; then
	echo "Usage: $0 [-h|--help]|[-c|--clean]"
	echo "This script does something."
	exit 0
fi

if [ "$1" == "-c" ] || [ "$1" == "--clean" ]; then
	zo data-set delete $data_set
	exit 0
fi

echo "$testing data set creation..."
zo data-set create $data_set
printf "$passed\n"

echo "$testing data set writing..."
printf "//IEFBR14$ JOB (IZUACCT),TEST,REGION=0m\n//RUN EXEC PGM=IEFBR14" | zo data-set write "$data_set_jcl"
printf "$passed\n"

echo "$testing view data set..."
zo data-set view $data_set_jcl
printf "$passed\n"

echo "$testing data set list..."
zo data-set list $data_set
printf "$passed\n"

echo "$testing data set list-members..."
zo data-set list-members $data_set
printf "$passed\n"

echo "$testing job list..."
zo job list
printf "$passed\n"

echo "$testing job submit..."
jobid=$(zo job submit "$data_set_jcl" --only-jobid true)
echo "Submitted job ${jobid}"
echo " "
sleep 1
printf "$passed\n"

echo "$testing delete data set..."
zo data-set delete $data_set
printf "$passed\n"

echo "$testing listing job files..."
zo job list-files ${jobid}
printf "$passed\n"

echo "$testing view job status..."
zo job view-status ${jobid}
printf "$passed\n"

echo "$testing view job files..."
zo job view-file ${jobid} 2
printf "$passed\n"

echo "$testing view job jcl..."
zo job view-jcl ${jobid}
printf "$passed\n"

echo "$testing delete job ..."
zo job delete ${jobid}
printf "$passed\n"

echo "$testing issuing conosle command ..."
zoa console issue "d iplinfo" --console-name zowe
printf "$passed\n"
