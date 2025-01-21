/**
 * @file mega/android/androidFileSystem.cpp
 * @brief Android filesystem/directory access
 *
 * (c) 2013-2024 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include <mega/android/androidFileSystem.h>
#include <mega/filesystem.h>
#include <mega/logging.h>

extern jclass fileWrapper;
extern jclass integerClass;
extern JavaVM* MEGAjvm;

namespace mega
{

AndroidPlatformURIHelper AndroidPlatformURIHelper::mPlatformHelper;
int AndroidPlatformURIHelper::mNumInstances = 0;
LRUCache<std::string, std::shared_ptr<AndroidFileWrapper>>
    AndroidFileWrapperRepository::mRepository(100);

AndroidFileWrapper::AndroidFileWrapper(const std::string& path):
    mPath(path)
{
    if (fileWrapper == nullptr)
    {
        LOG_err << "Error: AndroidFileWrapper::AndroidFileWrapper class not found";
        return;
    }

    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID getAndroidFileMethod = env->GetStaticMethodID(
        fileWrapper,
        GET_ANDROID_FILE,
        "(Ljava/lang/String;)Lmega/privacy/android/data/filewrapper/FileWrapper;");

    if (getAndroidFileMethod == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::AndroidFileWrapper";
        return;
    }

    jstring jPath = env->NewStringUTF(mPath.c_str());
    jobject temporalObject = env->CallStaticObjectMethod(fileWrapper, getAndroidFileMethod, jPath);
    env->DeleteLocalRef(jPath);

    if (temporalObject != nullptr)
    {
        mAndroidFileObject = env->NewGlobalRef(temporalObject);
        env->DeleteLocalRef(temporalObject);
    }
}

AndroidFileWrapper::~AndroidFileWrapper()
{
    if (mAndroidFileObject)
    {
        JNIEnv* env{nullptr};
        MEGAjvm->AttachCurrentThread(&env, NULL);
        env->DeleteGlobalRef(mAndroidFileObject);
    }
}

int AndroidFileWrapper::getFileDescriptor(bool write)
{
    if (!exists())
    {
        return -1;
    }

    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);

    jmethodID methodID =
        env->GetMethodID(fileWrapper, "getFileDescriptor", "(Z)Ljava/lang/Integer;");
    if (methodID == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::getFileDescriptor";
        return -1;
    }

    jobject fileDescriptorObj = env->CallObjectMethod(mAndroidFileObject, methodID, write);
    if (fileDescriptorObj && integerClass)
    {
        jmethodID intValueMethod = env->GetMethodID(integerClass, "intValue", "()I");
        if (!intValueMethod)
        {
            return -1;
        }

        return env->CallIntMethod(fileDescriptorObj, intValueMethod);
    }

    return -1;
}

bool AndroidFileWrapper::isFolder()
{
    if (!exists())
    {
        return false;
    }

    if (mIsFolder.has_value())
    {
        return mIsFolder.value();
    }

    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID = env->GetMethodID(fileWrapper, IS_FOLDER, "()Z");
    if (methodID == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::isFolder";
        return false;
    }

    mIsFolder = env->CallBooleanMethod(mAndroidFileObject, methodID);
    return mIsFolder.value();
}

string AndroidFileWrapper::getPath()
{
    return mPath;
}

bool AndroidFileWrapper::isURI()
{
    if (mIsURI.has_value())
    {
        return mIsURI.value();
    }

    constexpr char IS_PATH[] = "isPath";
    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID = env->GetStaticMethodID(fileWrapper, IS_PATH, "(Ljava/lang/String;)Z");
    if (methodID == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();

        LOG_err << "Critical error AndroidPlatformHelper::isURI";
        return false;
    }

    mIsURI = !env->CallStaticBooleanMethod(fileWrapper, methodID, env->NewStringUTF(mPath.c_str()));
    return mIsURI.value();
}

std::string AndroidFileWrapper::getName()
{
    if (!exists())
    {
        return std::string();
    }

    if (mName.has_value())
    {
        return mName.value();
    }

    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID = env->GetMethodID(fileWrapper, GET_NAME, "()Ljava/lang/String;");
    if (methodID == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::getName";
        return "";
    }

    jstring name = (jstring)env->CallObjectMethod(mAndroidFileObject, methodID);

    const char* nameStr = env->GetStringUTFChars(name, nullptr);
    mName = nameStr;
    env->ReleaseStringUTFChars(name, nameStr);
    return mName.value();
}

std::vector<std::shared_ptr<AndroidFileWrapper>> AndroidFileWrapper::getChildren()
{
    if (!exists())
    {
        return std::vector<std::shared_ptr<AndroidFileWrapper>>();
    }

    JNIEnv* env{nullptr};
    MEGAjvm->AttachCurrentThread(&env, NULL);
    jmethodID methodID = env->GetMethodID(fileWrapper, GET_CHILDREN_URIS, "()Ljava/util/List;");
    if (methodID == nullptr)
    {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOG_err << "Error: AndroidFileWrapper::getchildren";
        return std::vector<std::shared_ptr<AndroidFileWrapper>>();
    }

    jobject childrenUris = env->CallObjectMethod(mAndroidFileObject, methodID);
    jclass listClass = env->FindClass("java/util/List");
    jmethodID sizeMethod = env->GetMethodID(listClass, "size", "()I");
    jmethodID getMethod = env->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");
    jint size = env->CallIntMethod(childrenUris, sizeMethod);

    std::vector<std::shared_ptr<AndroidFileWrapper>> children;
    children.reserve(size);
    for (jint i = 0; i < size; ++i)
    {
        jstring element = (jstring)env->CallObjectMethod(childrenUris, getMethod, i);
        const char* elementStr = env->GetStringUTFChars(element, nullptr);
        children.push_back(AndroidFileWrapperRepository::getAndroidFileWrapper(elementStr));
        env->ReleaseStringUTFChars(element, elementStr);
        env->DeleteLocalRef(element);
    }

    return children;
}

bool AndroidFileWrapper::exists()
{
    return mAndroidFileObject != nullptr;
}

std::shared_ptr<AndroidFileWrapper>
    AndroidFileWrapperRepository::getAndroidFileWrapper(const std::string& path)
{
    auto androidFileWrapper = mRepository.get(path);
    if (androidFileWrapper.has_value())
    {
        return androidFileWrapper.value();
    }

    std::shared_ptr<AndroidFileWrapper> androidFileWrapperNew{new AndroidFileWrapper(path)};
    mRepository.put(path, androidFileWrapperNew);
    return androidFileWrapperNew;
}

AndroidPlatformURIHelper::AndroidPlatformURIHelper()
{
    if (mNumInstances == 0)
    {
        URIHandler::setPlatformHelper(this);
    }

    mNumInstances++;
}

bool AndroidPlatformURIHelper::isURI(const std::string& path)
{
    std::shared_ptr<AndroidFileWrapper> fileWrapper =
        AndroidFileWrapperRepository::getAndroidFileWrapper(path);
    if (fileWrapper->exists())
    {
        return fileWrapper->isURI();
    }

    return false;
}

std::string AndroidPlatformURIHelper::getName(const std::string& path)
{
    std::shared_ptr<AndroidFileWrapper> fileWrapper =
        AndroidFileWrapperRepository::getAndroidFileWrapper(path);
    if (fileWrapper->exists())
    {
        return fileWrapper->getName();
    }

    return std::string();
}

bool AndroidFileAccess::fopen(const LocalPath& f,
                              bool,
                              bool write,
                              FSLogging,
                              DirAccess*,
                              bool,
                              bool,
                              LocalPath*)
{
    fopenSucceeded = false;
    retry = false;
    std::string fstr = f.rawValue();
    assert(!mFileWrapper);

    mFileWrapper = AndroidFileWrapperRepository::getAndroidFileWrapper(fstr);

    if (!mFileWrapper->exists())
    {
        return false;
    }

    assert(fd < 0 && "There should be no opened file descriptor at this point");
    sysclose();

    fd = mFileWrapper->getFileDescriptor(write);
    if (fd < 0)
    {
        LOG_err << "Error getting file descriptor";
        errorcode = fd == -2 ? EACCES : ENOENT;
        return false;
    }

    struct stat statbuf;
    if (::fstat(fd, &statbuf) == -1)
    {
        errorcode = errno;
        LOG_err << "Failled to call fstat: " << errorcode << "  " << strerror(errorcode);
        close(fd);
        fd = -1;
        return false;
    }

    if (S_ISLNK(statbuf.st_mode))
    {
        LOG_err << "Sym links aren't supported in Android";
        return -1;
    }

    type = S_ISDIR(statbuf.st_mode) ? FOLDERNODE : FILENODE;
    size = (type == FILENODE || mIsSymLink) ? statbuf.st_size : 0;
    mtime = statbuf.st_mtime;
    fsid = (handle)statbuf.st_ino;
    fsidvalid = true;

    FileSystemAccess::captimestamp(&mtime);

    fopenSucceeded = true;
    return true;
}

void AndroidFileAccess::fclose()
{
    if (fd >= 0)
    {
        close(fd);
    }

    fd = -1;
}

bool AndroidFileAccess::fwrite(const byte* data, unsigned len, m_off_t pos)
{
    retry = false;
    lseek64(fd, pos, SEEK_SET);
    return write(fd, data, len) == len;
}

bool AndroidFileAccess::fstat(m_time_t& modified, m_off_t& size)
{
    struct stat attributes;

    retry = false;
    if (::fstat(fd, &attributes))
    {
        errorcode = errno;

        LOG_err << "Unable to stat descriptor: " << fd << ". Error was: " << errorcode;

        return false;
    }

    modified = attributes.st_mtime;
    size = static_cast<m_off_t>(attributes.st_size);

    return true;
}

bool AndroidFileAccess::ftruncate(m_off_t size)
{
    retry = false;

    // Truncate the file.
    if (::ftruncate(fd, size) == 0)
    {
        // Set the file pointer to the end.
        return lseek(fd, size, SEEK_SET) == size;
    }

    // Couldn't truncate the file.
    return false;
}

void AndroidFileAccess::updatelocalname(const LocalPath& name, bool force)
{
    if (force || !nonblocking_localname.empty())
    {
        nonblocking_localname = name;
    }
}

AndroidFileAccess::AndroidFileAccess(Waiter* w, int defaultfilepermissions, bool):
    FileAccess(waiter),
    mDefaultFilePermissions(defaultfilepermissions)
{}

AndroidFileAccess::~AndroidFileAccess() {}

std::shared_ptr<AndroidFileWrapper> AndroidFileAccess::stealFileWrapper()
{
    sysclose();
    return mFileWrapper;
}

bool AndroidFileAccess::sysread(byte* dst, unsigned len, m_off_t pos)
{
    retry = false;
    lseek64(fd, pos, SEEK_SET);
    return read(fd, (char*)dst, len) == len;
}

bool AndroidFileAccess::sysstat(m_time_t* mtime, m_off_t* size, FSLogging)
{
    if (!mFileWrapper)
    {
        mFileWrapper =
            AndroidFileWrapperRepository::getAndroidFileWrapper(nonblocking_localname.rawValue());
    }
    else
    {
        assert(nonblocking_localname.rawValue() == mFileWrapper->getName());
    }

    bool opened = false;
    if (fd < 0)
    {
        fd = mFileWrapper->getFileDescriptor(false);
        if (fd < 0)
        {
            errorcode = fd == -2 ? EACCES : ENOENT;
            LOG_err << "Error getting file descriptor";
            return false;
        }

        opened = true;
    }

    struct stat statbuf;
    if (::fstat(fd, &statbuf) == -1)
    {
        errorcode = errno;
        LOG_err << "Failled to call fstat: " << errorcode << "  " << strerror(errorcode);
        if (opened)
        {
            close(fd);
        }
        return false;
    }

    if (S_ISLNK(statbuf.st_mode))
    {
        LOG_err << "Sym links aren't supported in Android";
        return false;
    }

    retry = false;

    type = TYPE_UNKNOWN;

    errorcode = 0;
    if (S_ISDIR(statbuf.st_mode))
    {
        type = FOLDERNODE;
        if (opened)
        {
            close(fd);
            fd = -1;
        }
        return false;
    }

    type = FILENODE;
    *size = statbuf.st_size;
    *mtime = statbuf.st_mtime;

    FileSystemAccess::captimestamp(mtime);

    if (opened)
    {
        close(fd);
        fd = -1;
    }

    return true;
}

bool AndroidFileAccess::sysopen(bool, FSLogging)
{
    assert(fd < 0 && "There should be no opened file descriptor at this point");
    errorcode = 0;
    if (fd >= 0)
    {
        sysclose();
    }

    assert(mFollowSymLinks);

    mFileWrapper = AndroidFileWrapperRepository::getAndroidFileWrapper(
        nonblocking_localname.platformEncoded());

    if (!mFileWrapper->exists())
    {
        errorcode = ENOENT;
        return false;
    }

    fd = mFileWrapper->getFileDescriptor(false);
    if (fd < 0)
    {
        LOG_err << "Error getting file descriptor";
        errorcode = EACCES;
    }

    return fd >= 0;
}

void AndroidFileAccess::sysclose()
{
    assert(nonblocking_localname.empty() || fd >= 0);
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

bool AndroidDirAccess::dopen(LocalPath* path, FileAccess* f, bool doglob)
{
    mIndex = 0;
    if (f)
    {
        mFileWrapper = static_cast<AndroidFileAccess*>(f)->stealFileWrapper();
    }
    else
    {
        assert(path);
        std::string fstr = path->rawValue();
        assert(!mFileWrapper);

        mFileWrapper = AndroidFileWrapperRepository::getAndroidFileWrapper(fstr);
    }

    if (!mFileWrapper->exists())
    {
        return false;
    }

    mChildren = mFileWrapper->getChildren();
    return true;
}

bool AndroidDirAccess::dnext(LocalPath& path,
                             LocalPath& name,
                             bool followsymlinks,
                             nodetype_t* type)
{
    if (mChildren.size() <= mIndex)
    {
        return false;
    }

    auto& next = mChildren[mIndex];
    assert(next.get());
    path = std::move(LocalPath::fromPlatformEncodedAbsolute(next->getPath()));
    name = std::move(LocalPath::fromPlatformEncodedRelative(next->getName()));
    if (type)
    {
        *type = next->isFolder() ? FOLDERNODE : FILENODE;
    }

    mIndex++;
    return true;
}

} // namespace
