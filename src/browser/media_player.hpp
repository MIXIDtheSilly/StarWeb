#pragma once
#include <string>

// The build links exactly one backend behind impl_:
//   macOS         -> media_player_mac.mm     (AVFoundation)
//   Windows/Linux -> media_player_ffmpeg.cpp (FFmpeg + miniaudio)
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
    void* impl_;
};
