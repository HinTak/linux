#!/bin/bash
echo $0 $*

cookie=~/cookies.txt
if [ "$1" != "" ]; then
	THE_NAME=$1
else
	THE_NAME=$(whoami)
fi


make_cookie="http://168.219.243.151/secos/issueToken.do"
check_cookie="http://168.219.243.151/secos/checkCookie.do"
NOPROXY="--noproxy 168.219.243.151"

#make_cookie="http://10.40.65.145:8070/isms/open/masterkey/issueToken.do"
#check_cookie="http://10.40.65.145:8070/isms/open/masterkey/checkCookie.do"
#NOPROXY="--noproxy 10.40.65.145"


function curl_makeToken() {
	local user_name=$1

	echo "================================================================="
	echo "User :: $user_name"
	echo "================================================================="
	if [ -f /usr/sbin/ifconfig ] ; then
		/usr/sbin/ifconfig | grep inet
	elif [ -f /sbin/ifconfig ] ; then
		/sbin/ifconfig | grep inet
	fi
	echo "================================================================="

	curl_out=`curl --dump-header $cookie -w '\nHTTP:%{http_code}\nContet-Type:%{content_type}\n' -F "user=$user_name" -F "day=1" $make_cookie $NOPROXY`
	var=`printf "%s\n" $curl_out | grep HTTP: | sed 's/HTTP://'`
	echo var:   $var

	if [ "$var" -eq "200" ];then
		echo [ userID: $1 is authenticated. ]
		check_valid
		
		case $? in
		1)
			echo [ error:cookie file have some problem ]
		;;
		2)
			echo [ cookie is valid ]
			exit
		;;
		3)
			echo [ error:impossible ]
		;;
		esac


	elif [ "$var" -eq "401" ];then
		echo [ userID: $1 authentication failed. ]
	else
		echo [ undefined error ]
	fi
}

function check_valid() {

	if [  -f $cookie ]; then

		echo "Cookie : $cookie"
		curl_output=$(curl -b $cookie  $check_cookie $NOPROXY )
		echo "Curl Output : $curl_output"
		echo $curl_output | grep "token is valid"
		
		if [ $? == 0 ]; then
			return 2
		else
			return 1
		fi	

	else
		return 3
	fi

}

function run_try() {

	check_valid

	case $? in
	1)
		echo "[ Cookie is invalid ]"
		curl_makeToken $THE_NAME
	;;
	
	2)
		echo "[ cookie is vaild ]"
		exit
	;;
	3)
		echo "[ Cookie isnt exist ]"
		curl_makeToken $THE_NAME
	;;
	esac
}

# retry 5
run_try
run_try
run_try
run_try
run_try

echo "[ Error : $THE_NAME dont have permission. ]"












