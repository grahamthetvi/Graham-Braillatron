#include <iostream>
#include <speech-dispatcher/libspeechd.h>
#include <unistd.h>

int main() {
    std::cout << "Starting diagnostic test..." << std::endl;

    std::cout << "[Step A] Connecting to Speech Dispatcher in THREADED mode..." << std::endl;
    SPDConnection *conn_threaded = spd_open("test-threaded", "test", nullptr, SPD_MODE_THREADED);
    if (conn_threaded == nullptr) {
        std::cout << "Threaded connection failed." << std::endl;
        return 1;
    }
    std::cout << "Threaded connected! Saying hello..." << std::endl;
    int ret = spd_say(conn_threaded, SPD_MESSAGE, "Hello from threaded connection");
    std::cout << "spd_say returned: " << ret << std::endl;

    std::cout << "Sleeping 2 seconds..." << std::endl;
    sleep(2);

    std::cout << "Closing connection..." << std::endl;
    spd_close(conn_threaded);
    std::cout << "Threaded connection closed." << std::endl;
    return 0;
}
