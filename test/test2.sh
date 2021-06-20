#Set shell coloring for important messages
GREEN='\033[1;32m' #bold green
RESET_COLOR='\033[0m'

echo -e "${GREEN}Test is starting${RESET_COLOR}"
#Start server and background process for termination
bin/server -c config2.txt &
SERVER_PID=$!
export SERVER_PID
bash -c 'sleep 5 && kill -s SIGHUP ${SERVER_PID}' &
TIMER_PID=$!
export TIMER_PID

echo -e "${GREEN}FIRST TEST - verifying replacement for file capacity${RESET_COLOR}"
#Send 12 files with total size < 1MB, thus overflowing file capacity for *2* times.
#Expected output is that option '-D' is ignored because file expelling for creation does NOT cause sending them back to client
bin/client -p -f bin/tmp/serverSocket.sk -w test/test2files/minifiles -D test/test2files/minirecvs

echo -e "${GREEN}SECOND TEST - verifying replacement for storage capacity and rejecting too much big files${RESET_COLOR}"
#Send 3 files that shall cause multiple storage capacity misses, and save them in a separate folder, then send a file whose size is bigger than max storage cap.
#Expected output is that:
#	1. big1 shall expel the first lorem*.txt file encountered for file cap, then it shall be written on storage WITHOUT expelling anything else;
#	2. big2 shall expel another lorem*.txt file for file cap, then it shall expel ALL lorem*.txt files that are still hosted on storage and even big1 for storage cap;
#	3. big3 shall do nothing;
#	4. overflow shall be added as created file but it shall not be written on storage, thus it shall appear with 0 bytes.
#Finally, we expect to have *4* replacements for file cap (i.e. 4 files evicted by this) and *1* replacement for storage cap (i.e. 9 files evicted by this).
bin/client -p -f bin/tmp/serverSocket.sk -W test/test2files/big1, test/test2files/big2, test/test2files/big3, test/test2files/overflow -D test/test2files/bigrecvs

wait $SERVER_PID
wait $TIMER_PID

echo -e "${GREEN}Test ended${RESET_COLOR}"

exit 0
