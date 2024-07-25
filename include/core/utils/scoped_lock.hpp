#pragma once
#include "core/application.hpp"

namespace YAVE
{
class SDLScopedLock
{
public:
    explicit SDLScopedLock(SDL_mutex* mutex)
        : m_mutex(mutex)
    {
        SDL_LockMutex(m_mutex);
    }

    ~SDLScopedLock()
    {
        SDL_UnlockMutex(m_mutex);
    }

    void unlock()
    {
        delete this;
    }

private:
    SDL_mutex* m_mutex;
};
} // namespace YAVE