#pragma once

#include <deque>

#include "core/backend/video_player.hpp"

namespace YAVE
{
constexpr std::size_t MAX_PACKETS_NB = 24;

/**
 * @typedef PacketDeque
 * @brief An double-ended queue for audio and video packets.
 */
using PacketDeque = std::deque<AVPacket>;

/**
 * @typedef PacketDequeType
 * @brief The value type of the packet deque. (AVPacket)
 */
using PacketDequeType = PacketDeque::value_type;

class PacketQueue
{
public:
    PacketQueue();
    ~PacketQueue() = default;

    /**
     * @brief Adds a new packet to the packet deque.
     * @param src_packet The packet that will be added.
     * @return 0 <= for success, a negative integer for errors.
     */
    int enqueue(const AVPacket* src_packet);

    /**
     * @brief Removes the first packet.
     * @param dest_packet A pointer to the destination packet.
     * @return 0 <= for sucess, a negative integer for errors.
     */
    int dequeue(AVPacket* dest_packet);

    [[nodiscard]] inline bool isEmpty()
    {
        return m_nb_packets == 0;
    };
    [[nodiscard]] inline bool isFull()
    {
        return m_nb_packets >= MAX_PACKETS_NB;
    };

    inline void clear()
    {
        // Because reseting the packet queue, unreference the packets first.
        for (AVPacket packet : m_packet_deque) {
            av_packet_unref(&packet);
        }

        m_packet_deque.clear();
        m_nb_packets = 0;
    }

    [[nodiscard]] inline unsigned int getCount() const
    {
        return m_nb_packets;
    };

    [[nodiscard]] inline const PacketDequeType getFront() const
    {
        return m_packet_deque.front();
    }
    [[nodiscard]] inline const PacketDequeType getBack() const
    {
        return m_packet_deque.back();
    }

    static SDL_mutex* mutex;
    static SDL_cond* video_paused_cond;
    static SDL_cond* packet_availability_cond;

    static bool start_audio_dequeue;

private:
    PacketDeque m_packet_deque;
    unsigned int m_nb_packets;
};
} // namespace YAVE