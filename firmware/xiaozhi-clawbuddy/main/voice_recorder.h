#ifndef VOICE_RECORDER_H
#define VOICE_RECORDER_H

#include <string>

class AudioService;
class NetworkInterface;

struct VoiceRecorderResult {
    bool success = false;
    std::string path;
    std::string message;
    size_t bytes_written = 0;
    int duration_seconds = 0;
    int sample_rate = 16000;
};

struct VoiceTranscriptionResult {
    bool success = false;
    std::string response;
    std::string transcript;
    std::string sidecar_path;
    std::string message;
    int status_code = 0;
};

class VoiceRecorder {
public:
    static VoiceRecorderResult RecordWavToSd(AudioService& audio_service,
                                             const std::string& mount_point,
                                             int duration_seconds,
                                             const std::string& prefix = "recording");

    static VoiceTranscriptionResult TranscribeFile(NetworkInterface& network,
                                                   const std::string& file_path,
                                                   const std::string& url,
                                                   const std::string& model,
                                                   const std::string& language,
                                                   const std::string& prompt,
                                                   bool save_sidecar);

    static std::string BuildRecordingPath(const std::string& mount_point, const std::string& prefix);
};

#endif
