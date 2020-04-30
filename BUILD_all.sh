#!/bin/bash 
######################
# Author Harinath A
######################
######################
BUILD_COMMAND="BUILD.sh"
# hawkp 
HAWKP_TIZEN_DEBUG="1 3 n n y y y y y y"
HAWKP_TIZEN_PERF="1 2 n n y y y y y y"
HAWKP_TIZEN_RELEASE="1 1 n n y y y y y y"
# hawkm
HAWKM_TIZEN_DEBUG="2 3 n n y y y y y y"
HAWKM_TIZEN_PERF="2 2 n n y y y y y y"
HAWKM_TIZEN_RELEASE="2 1 n n y y y y y y"
# jazzm
JAZZM_TIZEN_DEBUG="21 3 n n y y y y y y"
JAZZM_TIZEN_PERF="21 2 n n y y y y y y"
JAZZM_TIZEN_RELEASE="21 1 n n y y y y y y"


TEMPORY_PATH="${HOME}/"
EXTENTION_NAT="_nat.sh"
change_inputs()
{
if [ $1 ] && [ -f $1 ];
then
  inpp=0
  sed -s 's/read /_nat_read /g' $1 > ${TEMPORY_PATH}${1}${EXTENTION_NAT}x
  TOTAL_LINESSS=`wc -l ${TEMPORY_PATH}${1}${EXTENTION_NAT}x |cut -d" " -f 1`
  TAILEDS=`expr $TOTAL_LINESSS - 1`;
  head -1 ${TEMPORY_PATH}${1}${EXTENTION_NAT}x > ${TEMPORY_PATH}${1}${EXTENTION_NAT}
  echo "_nat_read()" >> ${TEMPORY_PATH}${1}${EXTENTION_NAT}
  echo "{" >> ${TEMPORY_PATH}${1}${EXTENTION_NAT}
  echo "rm inputs_nat8" ${TEMPORY_PATH}${1}${EXTENTION_NAT}
  echo "read _myinput_" >> ${TEMPORY_PATH}${1}${EXTENTION_NAT}
  echo "export \$1=\"\${_myinput_}\"" >> ${TEMPORY_PATH}${1}${EXTENTION_NAT}
# echo "nat_ndelayed \${_myinput_}" >> ${TEMPORY_PATH}${1}${EXTENTION_NAT}
  echo "echo -e \"\\e[032m >>>\${_myinput_} <<<\\e[0m\" " >> ${TEMPORY_PATH}${1}${EXTENTION_NAT}
  echo "}" >> ${TEMPORY_PATH}${1}${EXTENTION_NAT}
  tail -${TAILEDS} ${TEMPORY_PATH}${1}${EXTENTION_NAT}x >> ${TEMPORY_PATH}${1}${EXTENTION_NAT}
  rm ${TEMPORY_PATH}${1}${EXTENTION_NAT}x
  chmod 0777 ${TEMPORY_PATH}${1}${EXTENTION_NAT}
fi
}
run_build_command()
{
	echo "$@" | sed "s/ /\\n/g" > ~/.nat_builder
	change_inputs ${BUILD_COMMAND}
	${TEMPORY_PATH}${BUILD_COMMAND}${EXTENTION_NAT} < ~/.nat_builder
}

echo "################################################"
echo "select one target to build (debug/perf/release)"
echo -e "\t1.HawkP-Tizen"
echo -e "\t2.HawkM-Tizen"
echo -e "\t3.JazzM-Tizen"
echo "################################################"
echo -en "|select>"
read txt
if [ $txt -eq 1 ];then
# HawkP tizen build all
run_build_command ${HAWKP_TIZEN_DEBUG}
run_build_command ${HAWKP_TIZEN_PERF}
run_build_command ${HAWKP_TIZEN_RELEASE}
elif [ $txt -eq 2 ];then
# HawkM tizen build all
run_build_command ${HAWKM_TIZEN_DEBUG}
run_build_command ${HAWKM_TIZEN_PERF}
run_build_command ${HAWKM_TIZEN_RELEASE}
elif [ $txt -eq 3 ];then
# JazzM tizen build all
run_build_command ${JAZZM_TIZEN_DEBUG}
run_build_command ${JAZZM_TIZEN_PERF}
run_build_command ${JAZZM_TIZEN_RELEASE}
else
echo "wrong choice ..."
fi


