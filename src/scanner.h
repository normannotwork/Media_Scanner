#pragma once
#include "media_state.h"
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

class MediaScanner {
    private:
        std::string root_path_;
        int interval_sec_;
        bool http_mode_;
        MediaState& shared_state_;
        
        std::atomic<bool> is_running_{false};
        std::thread worker_thread_;
        
        // без sleep_for и моментально просыпаться при вызове stop()
        std::mutex cv_mutex_;
        std::condition_variable cv_;

        std::unordered_map<std::string, std::string> ext_map_;
        
    public:
        MediaScanner(const std::string& root_path, int interval_sec, bool http_mode, MediaState& state);
        ~MediaScanner();

        void start();
        void stop();

    private:
        void init_extensions();
        void run();
        std::string scan_directory();
        void save_to_file(const std::string& json_data);
        std::string to_lower(const std::string& str);
};