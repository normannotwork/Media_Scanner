#pragma once
#include <string>
#include <shared_mutex>
#include <mutex>


class MediaState {
public:
    void set_json(const std::string& json_data) {
        std::unique_lock lock(mutex_);
        json_data_ = json_data;
    }

    std::string get_json() const {
        std::shared_lock lock(mutex_);
        return json_data_;
    }

private:
    std::string json_data_{"{\"audio\":[],\"video\":[],\"images\":[]}"};
    mutable std::shared_mutex mutex_;
};
