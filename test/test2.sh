#Set shell coloring for important messages
GREEN='\033[1;32m' #bold green
RESET_COLOR='\033[0m'

echo -e "${GREEN}Test is starting${RESET_COLOR}"
#Start server and background process for termination
bin/server -c config2.txt &
SERVER_PID=$!
export SERVER_PID
bash -c 'sleep 4 && kill -s SIGHUP ${SERVER_PID}' &
TIMER_PID=$!
export TIMER_PID

echo -e "${GREEN}FIRST TEST - verifying replacement for file capacity${RESET_COLOR}"
#Send 12 files with total size < 1MB, thus overflowing file capacity
#Expected output is that option '-D' is ignored because file expelling for creation does NOT cause sending them back to client
bin/client -p -f bin/tmp/serverSocket.sk -w test/test2files/minifiles -D test/test2files/minirecvs

echo -e "${GREEN}SECOND TEST - verifying replacement for storage capacity and rejecting too much big files${RESET_COLOR}"
#Send 3 files that shall cause multiple storage capacity misses, and save them in a separate folder, then send a file whose size is bigger than max storage cap.
#Expected output is that big1 shall expel ALL lorem*.txt files that are still hosted on storage, then big2 shall expel big1, big3 shall do nothing, overflow
#shall be added as created file but it shall not be written on storage, thus it shall appear with 0 bytes.
bin/client -p -f bin/tmp/serverSocket.sk -W test/test2files/big1, test/test2files/big2, test/test2files/big3, test/test2files/overflow -D test/test2files/bigrecvs

wait $SERVER_PID
wait $TIMER_PID

echo -e "${GREEN}Test ended${RESET_COLOR}"

exit 0
