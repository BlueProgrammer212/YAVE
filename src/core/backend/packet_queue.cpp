#include "core/backend/packet_queue.hpp"

namespace YAVE
{
PacketQueue::PacketQueue()
    : m_nb_packets(0)
{
}

SDL_mutex* PacketQueue::s_GlobalMutex = nullptr;
SDL_cond* PacketQueue::s_PacketAvailabilityCond = nullptr;

bool PacketQueue::start_audio_dequeue = false;

int PacketQueue::enqueue(const AVPacket* src_packet)
{
    constexpr int ENQUEUE_DELAY_MS = 100;

    if (isFull()) {
        SDL_Delay(ENQUEUE_DELAY_MS);
    }

    AVPacket dest_packet;

    if (av_packet_ref(&dest_packet, src_packet) < 0) {
        return -1;
    }

    m_packet_deque.push_back(dest_packet);
    m_nb_packets++;

    SDL_CondBroadcast(s_PacketAvailabilityCond);

    return 0;
}

int PacketQueue::dequeue(AVPacket* dest_packet)
{
    if (m_packet_deque.empty() || !dest_packet) {
        return -1;
    }

    // Create a copy of the packet and delete the first element
    if (av_packet_ref(dest_packet, &m_packet_deque.front()) < 0) {
        return -1;
    }

    av_packet_unref(&m_packet_deque.front());
    m_packet_deque.pop_front();
    m_nb_packets--;

    return 0;
}
} // namespace YAVE