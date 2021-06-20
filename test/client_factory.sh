GREEN='\033[1;32m'
RESET_COLOR='\033[0m'
#Get absolute path of current directory for option -r
SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

folder_index=$1 #Argument passed by test3.sh

client_launched=0

FOLDER="test3files/Files${folder_index}" #Folder assigned
ABSFOLDER="${SCRIPTPATH}/${FOLDER}" #For -r

echo -e "${GREEN}Client factory #$1 starting...${RESET_COLOR}"

while true; do

#Send folder and receive evicted files: each folder contains 10 files, thus having at most 40 server-requests for this client
bin/client -f bin/tmp/serverSocket.sk -w "test/${FOLDER}" -D "test/test3files/recv_${folder_index}_0"

#Read and save first 5 files: each of these client-requests triggers at most 15 server-requests
bin/client -f bin/tmp/serverSocket.sk -r ${ABSFOLDER}/file0, ${ABSFOLDER}/file1, ${ABSFOLDER}/file2, ${ABSFOLDER}/file3, ${ABSFOLDER}/file4\
 -d "test/test3files/recv_${folder_index}_1"

#Read and save last 5 files: as above for server-requests
bin/client -f bin/tmp/serverSocket.sk -r ${ABSFOLDER}/file5, ${ABSFOLDER}/file6, ${ABSFOLDER}/file7, ${ABSFOLDER}/file8, ${ABSFOLDER}/file9\
 -d "test/test3files/recv_${folder_index}_2"

client_launched=$(($client_launched + 3))

done

sleep 0.5 #For printing out ALL final messages without overlapping with server/client messages

echo -e "${GREEN}Client factory $1 terminated having launched ${client_launched} clients${RESET_COLOR}"

exit 0
