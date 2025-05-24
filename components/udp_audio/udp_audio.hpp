#pragma once

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/microphone/microphone.h"
#include "esphome/components/network/ip_address.h"
#include "esphome/components/socket/socket.h"

namespace esphome {
namespace udp_audio {

static const char *const TAG = "udp_audio";

class UDPAudioComponent : public Component {
 public:
  void setup() override {
    if (!this->setup_udp_socket_()) {
      this->mark_failed();
      return;
    }

    this->microphone_->add_data_callback([this](const std::vector<uint8_t> &data) {
      if (data.size() % 8 != 0) {  // 8 bytes per stereo frame (2 * 4 bytes)
        ESP_LOGW(TAG, "Received odd number of bytes (%u), expected multiple of 8 for stereo 32-bit", data.size());
        return;
      }

      const int32_t *samples = reinterpret_cast<const int32_t *>(data.data());
      size_t num_frames = data.size() / 8;  // Each frame has 2 int32_t (left and right)
      std::vector<int16_t> mono_data;
      mono_data.reserve(num_frames);

      // Process and convert to 16-bit mono
      int32_t last_left = samples[0];
      size_t repeat_count = 1;
      for (size_t i = 0; i < num_frames; i++) {
        int32_t left = samples[2 * i];
        int32_t right = samples[2 * i + 1];
        // Convert to 16-bit by taking upper 16 bits
        int16_t right_16 = static_cast<int16_t>(right >> 16);  // Use right channel
        // Check repetition on left channel (could use right if preferred)
        if (left == last_left) {
          repeat_count++;
        } else {
          if (repeat_count >= 5) {
            ESP_LOGW(TAG, "Detected %u repeated left samples: %d", repeat_count, last_left);
          }
          repeat_count = 1;
          last_left = left;
        }
        mono_data.push_back(right_16);  // Send right channel as mono
        // Alternatively, use left: int16_t left_16 = static_cast<int16_t>(left >> 16); mono_data.push_back(left_16);
      }
      if (repeat_count >= 5) {
        ESP_LOGW(TAG, "Detected %u repeated left samples: %d", repeat_count, last_left);
      }

      this->send_data_(mono_data);
    });
  }

  void set_microphone(microphone::Microphone *microphone) { this->microphone_ = microphone; }

  void set_address(network::IPAddress address, uint16_t port) {
    auto add_len = socket::set_sockaddr(reinterpret_cast<sockaddr *>(&this->dest_addr_), sizeof(this->dest_addr_),
                                        address.str(), port);
  }

  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 protected:
  void send_data_(const std::vector<int16_t> &data) {
    this->socket_->sendto(data.data(), data.size() * sizeof(int16_t), 0,
                          reinterpret_cast<const sockaddr *>(&this->dest_addr_), sizeof(this->dest_addr_));
  }

  bool setup_udp_socket_() {
    this->socket_ = socket::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (this->socket_ == nullptr) {
      ESP_LOGE(TAG, "Could not create socket");
      return false;
    }
    int enable = 1;
    int err = this->socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    if (err != 0) {
      ESP_LOGW(TAG, "Socket unable to set reuseaddr: errno %d", err);
    }
    err = this->socket_->setblocking(false);
    if (err != 0) {
      ESP_LOGE(TAG, "Socket unable to set nonblocking mode: errno %d", err);
      return false;
    }
    return true;
  }

  std::unique_ptr<socket::Socket> socket_ = nullptr;
  microphone::Microphone *microphone_;
  struct sockaddr_storage dest_addr_ {};
};

}  // namespace udp_audio
}  // namespace esphome