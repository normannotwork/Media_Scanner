#pragma once
#include <string>
#include <shared_mutex>
#include <mutex>

class MediaState {
    private:
        std::string json_data_{"{\"audio\":[],\"video\":[],\"images\":[]}"};
        mutable std::shared_mutex mutex_;
        
    public:
        void set_json(const std::string& json_data) {
            // Эксклюзивный доступ
            // Пока идет обновление строки, никто не может её читать.
            std::unique_lock lock(mutex_);
            json_data_ = json_data;
        }

        std::string get_json() const {
            // Разделяемый доступ.
            // Клиенты могут читать кэш одновременно без блокировки друг друга
            std::shared_lock lock(mutex_);
            return json_data_;
        }
};