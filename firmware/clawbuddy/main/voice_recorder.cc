#include "voice_recorder.h"

#include "audio/audio_service.h"
#include "boards/common/board.h"
#include "network_interface.h"
#include "http.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#define TAG "VoiceRecorder"

namespace {

bool EnsureDirectory(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    std::string current;
    for (char ch : path) {
        current.push_back(ch);
        if (ch != '/') {
            continue;
        }
        if (current.size() <= 1) {
            continue;
        }
        mkdir(current.c_str(), 0755);
    }
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

bool WriteWavHeader(FILE* file, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, uint32_t data_size) {
    if (file == nullptr) {
        return false;
    }

    const uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    const uint16_t block_align = channels * bits_per_sample / 8;
    const uint32_t riff_size = 36 + data_size;

    struct __attribute__((packed)) WavHeader {
        char riff[4];
        uint32_t riff_size;
        char wave[4];
        char fmt[4];
        uint32_t fmt_size;
        uint16_t audio_format;
        uint16_t channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits_per_sample;
        char data[4];
        uint32_t data_size;
    } header = {
        .riff = {'R','I','F','F'},
        .riff_size = riff_size,
        .wave = {'W','A','V','E'},
        .fmt = {'f','m','t',' '},
        .fmt_size = 16,
        .audio_format = 1,
        .channels = channels,
        .sample_rate = sample_rate,
        .byte_rate = byte_rate,
        .block_align = block_align,
        .bits_per_sample = bits_per_sample,
        .data = {'d','a','t','a'},
        .data_size = data_size,
    };

    rewind(file);
    return fwrite(&header, sizeof(header), 1, file) == 1;
}

std::string GetBaseName(const std::string& path) {
    auto pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string DeriveSidecarPath(const std::string& file_path, const char* ext) {
    auto dot = file_path.find_last_of('.');
    if (dot == std::string::npos) {
        return file_path + ext;
    }
    return file_path.substr(0, dot) + ext;
}

std::string JsonField(const std::string& body, const char* key) {
    const std::string quoted_key = std::string("\"") + key + "\"";
    auto key_pos = body.find(quoted_key);
    if (key_pos == std::string::npos) {
        return "";
    }
    auto colon = body.find(':', key_pos + quoted_key.size());
    if (colon == std::string::npos) {
        return "";
    }
    auto start = body.find('"', colon + 1);
    if (start == std::string::npos) {
        return "";
    }
    auto end = body.find('"', start + 1);
    if (end == std::string::npos || end <= start) {
        return "";
    }
    return body.substr(start + 1, end - start - 1);
}

} // namespace

std::string VoiceRecorder::BuildRecordingPath(const std::string& mount_point, const std::string& prefix) {
    int64_t now_us = esp_timer_get_time();
    std::string safe_prefix = prefix.empty() ? "recording" : prefix;
    return mount_point + "/recordings/" + safe_prefix + "-" + std::to_string(now_us / 1000) + ".wav";
}

VoiceRecorderResult VoiceRecorder::RecordWavToSd(AudioService& audio_service,
                                                 const std::string& mount_point,
                                                 int duration_seconds,
                                                 const std::string& prefix) {
    VoiceRecorderResult result;
    result.duration_seconds = duration_seconds;

    if (mount_point.empty()) {
        result.message = "SD card is not mounted";
        return result;
    }
    if (duration_seconds <= 0 || duration_seconds > 600) {
        result.message = "duration_seconds must be between 1 and 600";
        return result;
    }

    const std::string recordings_dir = mount_point + "/recordings";
    if (!EnsureDirectory(recordings_dir)) {
        result.message = "Failed to create recordings directory";
        return result;
    }

    result.path = BuildRecordingPath(mount_point, prefix);
    FILE* file = fopen(result.path.c_str(), "wb+");
    if (file == nullptr) {
        result.message = "Failed to open output file";
        return result;
    }

    if (!WriteWavHeader(file, result.sample_rate, 1, 16, 0)) {
        fclose(file);
        result.message = "Failed to write WAV header";
        return result;
    }

    const int chunk_ms = 20;
    const int samples_per_chunk = result.sample_rate * chunk_ms / 1000;
    uint32_t data_size = 0;

    for (int elapsed_ms = 0; elapsed_ms < duration_seconds * 1000; elapsed_ms += chunk_ms) {
        std::vector<int16_t> pcm;
        if (!audio_service.ReadAudioData(pcm, result.sample_rate, samples_per_chunk)) {
            fclose(file);
            result.message = "Microphone capture failed";
            return result;
        }

        if (pcm.empty()) {
            continue;
        }

        if (fwrite(pcm.data(), sizeof(int16_t), pcm.size(), file) != pcm.size()) {
            fclose(file);
            result.message = "Failed to write audio data";
            return result;
        }
        data_size += static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    }

    if (!WriteWavHeader(file, result.sample_rate, 1, 16, data_size)) {
        fclose(file);
        result.message = "Failed to finalize WAV header";
        return result;
    }

    fclose(file);
    result.bytes_written = data_size + 44;
    result.success = true;
    result.message = "Recording saved";
    return result;
}

VoiceTranscriptionResult VoiceRecorder::TranscribeFile(NetworkInterface& network,
                                                       const std::string& file_path,
                                                       const std::string& url,
                                                       const std::string& model,
                                                       const std::string& language,
                                                       const std::string& prompt,
                                                       bool save_sidecar) {
    VoiceTranscriptionResult result;

    if (file_path.empty() || url.empty()) {
        result.message = "file_path and url are required";
        return result;
    }

    FILE* file = fopen(file_path.c_str(), "rb");
    if (file == nullptr) {
        result.message = "Failed to open recording";
        return result;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size <= 0) {
        fclose(file);
        result.message = "Recording is empty";
        return result;
    }

    std::string boundary = "----XIAOZHI-VOICE-RECORDER-BOUNDARY";
    auto http = network.CreateHttp(30);
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    if (!http->Open("POST", url)) {
        fclose(file);
        result.message = "Failed to open transcription URL";
        return result;
    }

    auto write_field = [&](const char* name, const std::string& value) {
        if (value.empty()) {
            return;
        }
        std::string part;
        part += "--" + boundary + "\r\n";
        part += "Content-Disposition: form-data; name=\"" + std::string(name) + "\"\r\n\r\n";
        part += value + "\r\n";
        http->Write(part.c_str(), part.size());
    };

    write_field("model", model);
    write_field("language", language);
    write_field("prompt", prompt);

    std::string header;
    header += "--" + boundary + "\r\n";
    header += "Content-Disposition: form-data; name=\"file\"; filename=\"" + GetBaseName(file_path) + "\"\r\n";
    header += "Content-Type: audio/wav\r\n\r\n";
    http->Write(header.c_str(), header.size());

    char buffer[2048];
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        http->Write(buffer, read);
    }
    fclose(file);

    std::string footer = "\r\n--" + boundary + "--\r\n";
    http->Write(footer.c_str(), footer.size());
    http->Write("", 0);

    result.status_code = http->GetStatusCode();
    result.response = http->ReadAll();
    http->Close();

    if (result.status_code < 200 || result.status_code >= 300) {
        result.message = "Unexpected HTTP status: " + std::to_string(result.status_code);
        return result;
    }

    result.transcript = JsonField(result.response, "text");
    if (result.transcript.empty()) {
        result.transcript = result.response;
    }

    if (save_sidecar) {
        result.sidecar_path = DeriveSidecarPath(file_path, ".txt");
        FILE* sidecar = fopen(result.sidecar_path.c_str(), "wb");
        if (sidecar != nullptr) {
            fwrite(result.transcript.data(), 1, result.transcript.size(), sidecar);
            fclose(sidecar);
        }
    }

    result.success = true;
    result.message = "Transcription completed";
    return result;
}
