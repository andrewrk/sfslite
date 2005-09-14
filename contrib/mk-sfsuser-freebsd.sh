#
# Short shell script for making an SFS user/group if it does not exist
# alread on FreeBSD.
#

GROUP=sfs10
USER=sfs10
PW=pw
NAME='SFS User'

$PW groupshow $GROUP 2>&1 >/dev/null
if [ $? -ne 0 ]
then
    $PW groupadd $GROUP
    L=`$PW groupshow $GROUP`
    echo "Added sfs group: $L"
fi

$PW usershow $USER >/dev/null 2>&1
if [ $? -ne 0 ]
then
    $PW useradd $USER -g $GROUP -d /nonexistent -s /sbin/nologin -c "$NAME"
    L=`$PW usershow $USER`
    echo "Added sfs user: $L"
fi
