#pragma once

#include <atomic>
#include <memory>

using PreviewCancellationToken = std::shared_ptr<std::atomic_bool>;

inline PreviewCancellationToken makePreviewCancellationToken()
{
    return std::make_shared<std::atomic_bool>(false);
}

inline bool previewCancellationRequested(const PreviewCancellationToken& token)
{
    return token && token->load(std::memory_order_relaxed);
}

inline void cancelPreviewTask(PreviewCancellationToken& token)
{
    if (!token) {
        return;
    }

    token->store(true, std::memory_order_relaxed);
    token.reset();
}
