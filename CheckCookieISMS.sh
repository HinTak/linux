#!/bin/bash
echo $0 $*

cookie=~/cookies.txt
THE_NAME=$(whoami)


make_cookie="http://10.40.68.68/isms/open/masterkey/issueToken.do"
check_cookie="http://10.40.68.68/isms/open/masterkey/checkCookie.do"



function curl_makeToken() {
	local user_name=$1

	echo "User :: $user_name"

	curl_out=`curl --dump-header $cookie -w '\nHTTP:%{http_code}\nContet-Type:%{content_type}\n' -F "user=$1" -F "day=1" $make_cookie --noproxy 10.40.68.68`
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
		curl_output=$(curl -b $cookie  $check_cookie --noproxy 10.40.65.145 )
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












