#pragma once
#include <string>
#include <unordered_map>

class MediaScanner {
public:
    MediaScanner(const std::string& root_path, int interval_sec, bool http_mode);
    ~MediaScanner();

    void start();
    void stop();

private:
    std::string root_path_;
    int interval_sec_;
    bool http_mode_;
    
    std::unordered_map<std::string, std::string> ext_map_;

    void init();
    void run();
    std::string scan_directory();
    void save_to_file(const std::string& json_data);
    std::string to_lower(const std::string& str);
};
