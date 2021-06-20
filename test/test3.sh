#Setting shell colors for test messages
GREEN='\033[1;32m'
RESET_COLOR='\033[0m'

#Total time duration of test
TEST_TIME=30
echo -e "${GREEN}Test3 is starting${RESET_COLOR}"
echo -e "${GREEN}Please wait ${TEST_TIME} seconds...${RESET_COLOR}"

#Starting server
echo -e "${GREEN}Starting server...${RESET_COLOR}"
bin/server -c config3.txt &

SERVER_PID=$!
export SERVER_PID


#Background process for killing server after 30 seconds
bash -c "sleep ${TEST_TIME} && kill -s SIGINT ${SERVER_PID}" &
TIMER_PID=$!

#Array of client factories that handle continuous client launching for at least 30 seconds.
echo -e "${GREEN}Starting client factories...${RESET_COLOR}"
pids=()
for i in {0..10}; do
    bash -c "test/client_factory.sh ${i}" &
    pids+=($!)
    sleep 0.1
done


#First we wait server to dump its results
#All currently executing clients shall exit when receiving SIGPIPE 
wait ${SERVER_PID}
wait ${TIMER_PID}

echo -e "${GREEN}Server ended with status $?${RESET_COLOR}"

#Then we kill all client factories to terminate test and wait them
for i in "${pids[@]}"; do
	kill -s SIGKILL ${i}
	wait ${i}
done

#Kill all orphaned clients (if any)
kill -s SIGKILL $(pidof client)

#Finally we give confirmation of ending and exit
echo -e "${GREEN}Test3 ended${RESET_COLOR}"

exit 0
