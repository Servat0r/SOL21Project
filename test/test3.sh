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

#We use this method for sending stop time to client factories in order to get how many client each factory has launched
current_date=$(date +%s)
stop_date=$(echo "$current_date + ${TEST_TIME}" | bc)

#Background process for killing server after 30 seconds
bash -c "sleep ${TEST_TIME} && kill -s SIGINT ${SERVER_PID}" &

#Array of subprocesses ('client factories') that handle continuous client launching for at least 30 seconds.
echo -e "${GREEN}Starting client factories...${RESET_COLOR}"
pids=()
for i in {0..10}; do
    bash -c "test/client_factory.sh ${i} ${stop_date}" &
    pids+=($!)
    sleep 0.1
done

#First we wait server to dump its results
wait ${SERVER_PID}

echo -e "${GREEN}Server ended${RESET_COLOR}"

#Then we wait all client factories to dump how many client each one has launched
for i in "${pids[@]}"; do
	wait ${i}
done

#Finally we give confirmation of ending and exit
echo -e "${GREEN}Test3 ended${RESET_COLOR}"

exit 0
