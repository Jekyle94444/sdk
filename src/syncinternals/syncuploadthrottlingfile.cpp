/**
 * @file syncuploadthrottlingfile.cpp
 * @brief Class for UploadThrottlingFile.
 */

#ifdef ENABLE_SYNC

#include "mega/syncinternals/syncuploadthrottlingfile.h"

#include "mega/logging.h"

namespace mega
{

void UploadThrottlingFile::bypassThrottlingNextTime(const unsigned maxUploadsBeforeThrottle)
{
    if (mUploadCounter >= maxUploadsBeforeThrottle)
    {
        mBypassThrottlingNextTime = true;
    }
}

bool UploadThrottlingFile::checkUploadThrottling(
    const unsigned maxUploadsBeforeThrottle,
    const std::chrono::seconds uploadCounterInactivityExpirationTime)
{
    if (mBypassThrottlingNextTime)
    {
        mBypassThrottlingNextTime = false;
        return false;
    }

    if (const auto timeSinceLastUploadCounterProcess =
            std::chrono::steady_clock::now() - mUploadCounterLastTime;
        timeSinceLastUploadCounterProcess >= uploadCounterInactivityExpirationTime)
    {
        // Reset the upload counter if enough time has lapsed since last time.
        mUploadCounter = 0;
        mUploadCounterLastTime = std::chrono::steady_clock::now();
        return false;
    }

    return mUploadCounter >= maxUploadsBeforeThrottle;
}

bool UploadThrottlingFile::handleAbortUpload(SyncUpload_inClient& upload,
                                             const FileFingerprint& fingerprint,
                                             const unsigned maxUploadsBeforeThrottle,
                                             const LocalPath& transferPath)
{
    // 1. Upload cannot be aborted.
    if (upload.putnodesStarted)
        return false;

    if (!upload.wasStarted)
    {
        assert(!upload.wasTerminated);
        LOG_verbose << "Updating fingerprint of queued upload " << transferPath;
        upload.updateFingerprint(fingerprint);
        return false;
    }

    // 2. Upload must be aborted
    // If the upload is going to be aborted either due to a change while it was inflight or after a
    // failure, and the file was being throttled, let it start immediately next time.
    bypassThrottlingNextTime(maxUploadsBeforeThrottle);
    return true;
}

} // namespace mega

#endif // ENABLE_SYNC
