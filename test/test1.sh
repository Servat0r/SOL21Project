GREEN='\033[0;32m'
RESET_COLOR='\033[0m'
# get absolute path of current directory for the -r flag (files are saved on the server using their absolute path)
SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )" #.../SOL21Project/test

echo -e "${GREEN}Test is starting${RESET_COLOR}"

# start server with test config file
valgrind --leak-check=full bin/server -c config1.txt &
SERVER_PID=$!
export SERVER_PID
bash -c 'sleep 15 && kill -1 ${SERVER_PID}' &
TIMER_PID=$!

# write 'file1' and 'file2' from subdir 'test1files', then read them from the server and store them in subdir 'test1dest1'
bin/client -p -t 200 -f bin/tmp/serverSocket.sk -W test/test1files/file1, test/test1files/file2 -r ${SCRIPTPATH}/test1files/file1,${SCRIPTPATH}/test1files/file2 -d test/test1files/test1dest1

# write all files and dirs in subdir 'test1files/rec', then read all files from the server and store them in subdir 'test1dest2'
bin/client -p -t 200 -f bin/tmp/serverSocket.sk -w test/test1files/rec,0  -R 0 -d test/test1files/test1dest2

# lock a file and then delete it
bin/client -p -t 200 -f bin/tmp/serverSocket.sk -l ${SCRIPTPATH}/test1files/file1 -c ${SCRIPTPATH}/test1files/file1

# lock a file and unlock it after a second; another client tries to lock it but has to wait
bin/client -p -t 1000 -f bin/tmp/serverSocket.sk -l ${SCRIPTPATH}/test1files/file2 -u ${SCRIPTPATH}/test1files/file2 &

echo -e "${GREEN}Trying to lock the file, but having to wait...${RESET_COLOR}"

bin/client -p -t 200 -f bin/tmp/serverSocket.sk -l ${SCRIPTPATH}/test1files/file2

wait $TIMER_PID
wait $SERVER_PID

echo -e "${GREEN}Test ended${RESET_COLOR}"

exit 0
