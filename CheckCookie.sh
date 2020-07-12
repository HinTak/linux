#!/bin/bash
echo $0 $*

cookie=~/cookies.txt
THE_NAME=$(whoami)

ISMS_SERVER="10.40.68.68"
VD_AUTH_SERVER="168.219.243.151"
DEVELOPMENT_SERVER="10.40.65.145"

ISMS_URL="http://$ISMS_SERVER/isms/open/masterkey"
VD_AUTH_URL="http://$VD_AUTH_SERVER/secos"
DEVELOPMENT_URL="http://$DEVELOPMENT_SERVER:8070/isms/open/masterkey"

make_cookie=""
check_cookie=""
noproxy=""

function select_server() {
	local server=$1
	if [ "$server" = "vdserver" ]; then
		make_cookie="$VD_AUTH_URL/issueToken.do"
		check_cookie="$VD_AUTH_URL/checkCookie.do"
		noproxy="$VD_AUTH_SERVER"
	elif [ "$server" = "development" ]; then
		make_cookie="$DEVELOPMENT_URL/issueToken.do"
		check_cookie="$DEVELOPMENT_URL/checkCookie.do"
		noproxy="$DEVELOPMENT_SERVER"
	else
		make_cookie="$ISMS_URL/issueToken.do"
		check_cookie="$ISMS_URL/checkCookie.do"
		noproxy="$ISMS_SERVER"
	fi
	echo $make_cookie
	echo $check_cookie
}

function curl_makeToken() {
	local user_name=$1

	echo "User :: $user_name"

	echo "curl --retry 3 --dump-header $cookie -w '\nHTTP:%{http_code}\nContet-Type:%{content_type}\n' -F "user=$1" -F "day=1" $make_cookie --noproxy $noproxy"
	curl_out=`curl --retry 3 --dump-header $cookie -w '\nHTTP:%{http_code}\nContet-Type:%{content_type}\n' -F "user=$1" -F "day=1" $make_cookie --noproxy $noproxy`
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
		;;
		3)
			echo [ error:impossible ]
		;;
		esac

		exit

	elif [ "$var" -eq "401" ];then
		echo [ userID: $1 authentication failed. ]
	else
		echo [ undefined error ]
		exit
	fi
}

function check_valid() {
	if [  -f $cookie ]; then

		echo "Cookie : $cookie"
		curl_output=$(curl --retry 3 -b $cookie $check_cookie --noproxy $noproxy)
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

select_server $1
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


read -p "Single ID: " name
curl_makeToken $name

echo "[ Error : $THE_NAME & $name dont have permission. ]"












