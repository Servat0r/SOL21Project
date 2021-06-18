# get absolute path of current directory for the -r flag (files are saved on the server using their absolute path)
SCRIPTPATH="$( cd -- "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )" #.../SOL21Project/test
echo ${SCRIPTPATH}

# start server with test config file
valgrind --leak-check=full bin/server -c config1.txt &
SERVER_PID=$!
export SERVER_PID
bash -c 'sleep 10 && kill -1 ${SERVER_PID}' &
TIMER_PID=$!

# write 'file1' and 'file2' from subdir 'dummyFiles', then read them from
# the server and store them in subdir `test1dest1`
bin/client -p -t 200 -f bin/tmp/serverSocket.sk -W test/dummyFiles/file1, test/dummyFiles/file2 -r ${SCRIPTPATH}/dummyFiles/file1,${SCRIPTPATH}/dummyFiles/file2 -d test/test1dest1

# write all files and dirs in subdir `dummyFiles/rec`, then read all files from 
# the server and store them in subdir `test1dest2`
bin/client -p -t 200 -f bin/tmp/serverSocket.sk -w test/dummyFiles/rec,0  -R 0 -d test/test1dest2

# lock a file and then delete it
bin/client -p -t 200 -f bin/tmp/serverSocket.sk -l ${SCRIPTPATH}/dummyFiles/file1 -c ${SCRIPTPATH}/dummyFiles/file1

# lock a file and unlock it after a second; another client tries to lock it but has to wait
bin/client -p -t 1000 -f bin/tmp/serverSocket.sk -l ${SCRIPTPATH}/dummyFiles/file2 -u ${SCRIPTPATH}/dummyFiles/file2 &
echo "Trying to lock the file, but having to wait..."
bin/client -p -t 200 -f bin/tmp/serverSocket.sk -l ${SCRIPTPATH}/dummyFiles/file2

wait $TIMER_PID
wait $SERVER_PID

exit 0
