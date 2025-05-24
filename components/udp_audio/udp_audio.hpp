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
      if (data.size() % 4 != 0) {
        ESP_LOGW(TAG, "Received odd number of bytes (%u), expected multiple of 4 for stereo 16-bit", data.size());
        return;
      }

      ESP_LOGD(TAG, "Data buffer size: %u bytes", data.size());
      const int16_t *samples = reinterpret_cast<const int16_t *>(data.data());
      size_t num_samples = data.size() / sizeof(int16_t);
      std::vector<int16_t> mono_data;
      mono_data.reserve(num_samples / 2);

      // Log samples
      size_t log_limit = std::min<size_t>(20, num_samples);
      for (size_t i = 0; i < log_limit; i += 2) {
        ESP_LOGD(TAG, "Sample %u - Left: %d (0x%04X), Right: %d (0x%04X)", i / 2,
                 samples[i], static_cast<uint16_t>(samples[i]), samples[i + 1], static_cast<uint16_t>(samples[i + 1]));
      }

      // Check for repetition
      int16_t last_left = samples[0];
      size_t repeat_count = 1;
      for (size_t i = 2; i < num_samples; i += 2) {
        if (samples[i] == last_left) {
          repeat_count++;
        } else {
          if (repeat_count >= 5) {
            ESP_LOGW(TAG, "Detected %u repeated left samples: %d", repeat_count, last_left);
          }
          repeat_count = 1;
          last_left = samples[i];
        }
        //mono_data.push_back(samples[i]); // Left channel
        // Test right channel by commenting above and uncommenting below
        mono_data.push_back(samples[i + 1]); // Right channel
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
    ESP_LOGD(TAG, "Sending %u mono samples (%u bytes)", data.size(), data.size() * sizeof(int16_t));
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