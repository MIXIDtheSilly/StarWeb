#pragma once
#include <string>
#include <chrono>

class VideoPlayer {
public:
    VideoPlayer(const std::string& filepath, bool audio_only = false);
    ~VideoPlayer();

    void play();
    void pause();
    void update();
    void seek(double seconds);
    
    void set_volume(float vol);
    void set_muted(bool mute);
    void set_loop(bool lp);

    bool is_playing() const;
    double get_current_time() const;
    double get_duration() const;
    float get_volume() const;
    bool is_muted() const;
    bool is_audio_only() const;
    bool is_looping() const;

    unsigned int get_texture_id() const;
    int get_width() const;
    int get_height() const;

private:
#ifdef __APPLE__
    void* impl_;
#else
    std::string filepath_;
    bool is_audio_only_;
    bool is_playing_;
    double current_time_;
    double duration_;
    float volume_;
    bool is_muted_;
    bool loop_;
    std::chrono::steady_clock::time_point last_update_time_;
#endif
};
