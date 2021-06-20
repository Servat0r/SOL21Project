#Dummy test to verify that a SIGINT cause termination even with many clients still active
GREEN='\033[1;32m'
RESET='\033[0m'

SOCKET='bin/tmp/serverSocket.sk'

echo -e "${GREEN}Test0 starting${RESET}"

bin/server -c config3.txt &
SERVER_PID=$!
export SERVER_PID

#Sends the signal specified as first argument
bash -c "sleep 3 && kill -n $1 ${SERVER_PID}" &
TIMER_PID=$!
export TIMER_PID

#Spawns 30 clients that take at least 4.5 seconds to send ALL files with '-w' 
for i in {0..9}; do

bin/client -f ${SOCKET} -t 500 -w test/test3files/Files0 &

bin/client -f ${SOCKET} -t 500 -w test/test3files/Files1 &

bin/client -f ${SOCKET} -t 500 -w test/test3files/Files2 &

done

wait ${SERVER_PID}
wait ${TIMER_PID}

echo -e "${GREEN}Test ended${RESET}"

exit 0
