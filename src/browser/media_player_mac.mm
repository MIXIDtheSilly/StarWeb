#ifdef __APPLE__
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <TargetConditionals.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include "media_player.hpp"
#include "imgui.h"
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

@interface ObjCVideoPlayer : NSObject
@property (nonatomic, strong) AVPlayer* player;
@property (nonatomic, strong) AVPlayerItemVideoOutput* videoOutput;
@property (nonatomic, assign) GLuint textureId;
@property (nonatomic, assign) int width;
@property (nonatomic, assign) int height;
@property (nonatomic, assign) BOOL isPlaying;
@property (nonatomic, assign) double duration;
@property (nonatomic, assign) double currentTime;
@property (nonatomic, assign) float volume;
@property (nonatomic, assign) BOOL loop;
@property (nonatomic, assign) BOOL muted;
@property (nonatomic, assign) BOOL isAudioOnly;

- (instancetype)initWithPath:(NSString*)path audioOnly:(BOOL)audioOnly;
- (void)play;
- (void)pause;
- (void)update;
- (void)seek:(double)seconds;
- (void)setVolume:(float)vol;
- (void)setMuted:(BOOL)mute;
- (void)setLoop:(BOOL)lp;
@end

@implementation ObjCVideoPlayer

- (instancetype)initWithPath:(NSString*)path audioOnly:(BOOL)audioOnly {
    self = [super init];
    if (self) {
        _isAudioOnly = audioOnly;
        _volume = 1.0f;
        _isPlaying = false;
        _duration = 0.0;
        _currentTime = 0.0;
        _loop = false;
        _muted = false;
        _width = 0;
        _height = 0;
        _textureId = 0;
        
        NSURL* url = [NSURL fileURLWithPath:path];
        _player = [[AVPlayer alloc] initWithURL:url];
        _player.automaticallyWaitsToMinimizeStalling = NO;
        
        if (!_isAudioOnly) {
            NSDictionary* settings = @{
                (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
            };
            _videoOutput = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:settings];
            [_player.currentItem addOutput:_videoOutput];

            glGenTextures(1, &_textureId);
            glBindTexture(GL_TEXTURE_2D, _textureId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    }
    return self;
}

- (void)dealloc {
    [_player pause];
    if (_textureId != 0) {
        glDeleteTextures(1, &_textureId);
    }
    _player = nil;
    _videoOutput = nil;
}

- (void)play {
    // If playback has reached the end, restart from the beginning instead of
    // no-opping (AVPlayer won't play when the playhead is already at the end).
    if (_duration > 0.0 && _currentTime >= _duration - 0.1) {
        [self seek:0.0];
    }
    [_player play];
    _isPlaying = true;
}

- (void)pause {
    [_player pause];
    _isPlaying = false;
}

- (void)seek:(double)seconds {
    CMTime targetTime = CMTimeMakeWithSeconds(seconds, 600);
    [_player seekToTime:targetTime toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
}

- (void)setVolume:(float)vol {
    _volume = vol;
    _player.volume = _muted ? 0.0f : _volume;
}

- (void)setMuted:(BOOL)mute {
    _muted = mute;
    _player.volume = _muted ? 0.0f : _volume;
}

- (void)setLoop:(BOOL)lp {
    _loop = lp;
}

- (void)update {
    if (!_player) return;
    
    CMTime time = _player.currentTime;
    _currentTime = CMTimeGetSeconds(time);
    if (isnan(_currentTime) || isinf(_currentTime)) {
        _currentTime = 0.0;
    }
    
    AVPlayerItem* item = _player.currentItem;
    if (item) {
        CMTime dur = item.duration;
        if (CMTIME_IS_VALID(dur) && !CMTIME_IS_INDEFINITE(dur)) {
            _duration = CMTimeGetSeconds(dur);
        }
        
        if (_loop && _currentTime >= _duration - 0.1 && _duration > 0.0) {
            [self seek:0.0];
            [_player play];
        }
        
        if (!_loop && _currentTime >= _duration && _duration > 0.0) {
            _isPlaying = false;
        }
    }
    
    if (_isAudioOnly || !_videoOutput) return;
    
    CMTime itemTime = [_videoOutput itemTimeForHostTime:CACurrentMediaTime()];
    if (!CMTIME_IS_VALID(itemTime)) {
        itemTime = _player.currentTime;
    }
    
    if ([_videoOutput hasNewPixelBufferForItemTime:itemTime]) {
        CVPixelBufferRef pixelBuffer = [_videoOutput copyPixelBufferForItemTime:itemTime itemTimeForDisplay:NULL];
        if (pixelBuffer) {
            CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            
            int w = (int)CVPixelBufferGetWidth(pixelBuffer);
            int h = (int)CVPixelBufferGetHeight(pixelBuffer);
            void* baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
            
            glBindTexture(GL_TEXTURE_2D, _textureId);
            if (_width != w || _height != h) {
                _width = w;
                _height = h;
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _width, _height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, baseAddress);
            } else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _width, _height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, baseAddress);
            }
            
            CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            CVPixelBufferRelease(pixelBuffer);
        }
    }
}
@end

// C++ Class wrapper implementations
VideoPlayer::VideoPlayer(const std::string& filepath, bool audio_only) {
    impl_ = (__bridge_retained void*)[[ObjCVideoPlayer alloc] initWithPath:[NSString stringWithUTF8String:filepath.c_str()] audioOnly:audio_only];
}

VideoPlayer::~VideoPlayer() {
    ObjCVideoPlayer* player = (__bridge_transfer ObjCVideoPlayer*)impl_;
    (void)player;
}

void VideoPlayer::play() {
    [(__bridge ObjCVideoPlayer*)impl_ play];
}

void VideoPlayer::pause() {
    [(__bridge ObjCVideoPlayer*)impl_ pause];
}

void VideoPlayer::seek(double seconds) {
    [(__bridge ObjCVideoPlayer*)impl_ seek:seconds];
}

void VideoPlayer::set_volume(float vol) {
    [(__bridge ObjCVideoPlayer*)impl_ setVolume:vol];
}

void VideoPlayer::set_muted(bool mute) {
    [(__bridge ObjCVideoPlayer*)impl_ setMuted:mute];
}

void VideoPlayer::set_loop(bool lp) {
    [(__bridge ObjCVideoPlayer*)impl_ setLoop:lp];
}

void VideoPlayer::update() {
    [(__bridge ObjCVideoPlayer*)impl_ update];
}

bool VideoPlayer::is_playing() const {
    return ((__bridge ObjCVideoPlayer*)impl_).isPlaying;
}

double VideoPlayer::get_current_time() const {
    return ((__bridge ObjCVideoPlayer*)impl_).currentTime;
}

double VideoPlayer::get_duration() const {
    return ((__bridge ObjCVideoPlayer*)impl_).duration;
}

float VideoPlayer::get_volume() const {
    return ((__bridge ObjCVideoPlayer*)impl_).volume;
}

bool VideoPlayer::is_muted() const {
    return ((__bridge ObjCVideoPlayer*)impl_).muted;
}

bool VideoPlayer::is_audio_only() const {
    return ((__bridge ObjCVideoPlayer*)impl_).isAudioOnly;
}

bool VideoPlayer::is_looping() const {
    return ((__bridge ObjCVideoPlayer*)impl_).loop;
}

unsigned int VideoPlayer::get_texture_id() const {
    return ((__bridge ObjCVideoPlayer*)impl_).textureId;
}

int VideoPlayer::get_width() const {
    return ((__bridge ObjCVideoPlayer*)impl_).width;
}

int VideoPlayer::get_height() const {
    return ((__bridge ObjCVideoPlayer*)impl_).height;
}
#endif
