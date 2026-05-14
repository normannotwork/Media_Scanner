#include "scanner.h"
#include "server.h"
#include "media_state.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>


int main(int argc, char* argv[]) {
    std::string path = get_home_directory();
    int interval = 60;

    try {
        MediaState state;
        MediaScanner scanner(path, interval, 0, state);
        
        scanner.start();
        scanner.stop();
        
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
