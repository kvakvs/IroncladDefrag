#pragma once

#include <exception>
#include <memory>
#include <stop_token>
#include <utility>

namespace icd::core {

    // Thrown by CancelToken::throwIfCancelled() so deep code can leave a cancelled job in one step.
    class OperationCancelled : public std::exception {
    public:
        // Fixed text; cancellation is not an error and carries no details.
        [[nodiscard]] const char* what() const noexcept override { return "operation cancelled"; }
    };

    // Read side of cooperative cancellation: long jobs poll it between work items and stop early.
    // Cheap to copy; a default-constructed token is never cancelled.
    class CancelToken {
    public:
        CancelToken() = default;

        // Wraps a standard stop token (used by CancelSource and by std::jthread workers).
        explicit CancelToken(std::stop_token token) : token_(std::move(token)) {}

        // Whether cancellation was requested.
        [[nodiscard]] bool cancelled() const noexcept { return token_.stop_requested(); }

        // Throws OperationCancelled when cancellation was requested.
        void throwIfCancelled() const {
            if (cancelled()) {
                throw OperationCancelled();
            }
        }

        // The underlying token, for std::stop_callback and std::condition_variable_any waits.
        [[nodiscard]] const std::stop_token& stdToken() const noexcept { return token_; }

    private:
        std::stop_token token_; // Shared state with the owning source.
    };

    // Write side: whoever starts a job owns a source and cancels it (UI button, shutdown, parent job).
    class CancelSource {
    public:
        // An independent source.
        CancelSource() = default;

        // A source that is also cancelled when parent is, so cancelling a job cancels its sub-jobs.
        explicit CancelSource(const CancelToken& parent);

        // Requests cancellation; idempotent and safe from any thread.
        void cancel() noexcept { source_.request_stop(); }

        // Whether cancellation was requested on this source (directly or through the parent).
        [[nodiscard]] bool cancelled() const noexcept { return source_.stop_requested(); }

        // A token observing this source.
        [[nodiscard]] CancelToken token() const { return CancelToken(source_.get_token()); }

    private:
        // Callback that forwards a parent's cancellation to this source.
        struct Forward {
            std::stop_source target; // Source to cancel; shares state, so moving CancelSource is safe.

            // Invoked by the parent token on cancellation.
            void operator()() noexcept { target.request_stop(); }
        };

        std::stop_source source_; // This source's state.
        std::unique_ptr<std::stop_callback<Forward>> link_; // Parent link; null for independent sources.
    };

} // namespace icd::core
