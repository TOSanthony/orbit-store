// In-process reader only. 7-Zip never receives a destination path, never opens
// volumes, and never writes files itself. Orbit validates every entry first.
#include "sevenzip.h"
#include "Common/MyInitGuid.h"
#include "Common/MyCom.h"
#include "Common/UTFConvert.h"
#include "Windows/PropVariant.h"
#include "7zip/Archive/IArchive.h"
#include "7zip/IPassword.h"
#include "7zip/ICoder.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <climits>
#include <new>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <string>

STDAPI CreateArchiver(const GUID *, const GUID *, void **);
Z7_DEFINE_GUID(CLSID_CArchiveHandler, 0x23170F69, 0x40C1, 0x278A, 0x10,0,0,1,0x10,0,0,0);
using NWindows::NCOM::CPropVariant;
using namespace NArchive::NExtract;
struct Context {
    const SevenCallbacks *cb;
    void *user;
    UString password;
    bool passwordAsked = false;
    int result = 0;
    HRESULT poll() {
        if (cb->stopped(user)) { result = 1; return E_ABORT; }
        return result ? E_ABORT : S_OK;
    }
    HRESULT pass(BSTR *out) {
        passwordAsked = true;
        if (password.IsEmpty()) { result = -2; return E_ABORT; }
        return StringToBstr(password, out);
    }
};
class Input final: public IInStream, public CMyUnknownImp {
    Z7_IFACES_IMP_UNK_1(IInStream)
    Z7_IFACE_COM7_IMP(ISequentialInStream)
public:
    int fd;
    Context *ctx;
};
Z7_COM7F_IMF(Input::Read(void *data, UInt32 size, UInt32 *count)) {
    if (count) *count = 0;
    RINOK(ctx->poll())
    ssize_t n;
    do n = read(fd, data, size); while (n < 0 && errno == EINTR);
    if (n < 0) return E_FAIL;
    if (count) *count = (UInt32)n;
    return S_OK;
}
Z7_COM7F_IMF(Input::Seek(Int64 offset, UInt32 origin, UInt64 *pos)) {
    RINOK(ctx->poll())
    if (origin > 2) return E_INVALIDARG;
    off_t n = lseek(fd, (off_t)offset, origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END);
    if (n < 0) return E_FAIL;
    if (pos) *pos = n;
    return S_OK;
}
class Open final: public IArchiveOpenCallback, public ICryptoGetTextPassword, public CMyUnknownImp {
    Z7_IFACES_IMP_UNK_2(IArchiveOpenCallback, ICryptoGetTextPassword)
public:
    Context *ctx;
};
Z7_COM7F_IMF(Open::SetTotal(const UInt64 *, const UInt64 *)) { return ctx->poll(); }
Z7_COM7F_IMF(Open::SetCompleted(const UInt64 *, const UInt64 *)) { return ctx->poll(); }
Z7_COM7F_IMF(Open::CryptoGetTextPassword(BSTR *out)) { return ctx->pass(out); }
struct Entry { std::string path; int64_t size; bool directory; };
class Output final: public ISequentialOutStream, public CMyUnknownImp {
public:
    Z7_IFACES_IMP_UNK_1(ISequentialOutStream)
public:
    Context *ctx;
};
Z7_COM7F_IMF(Output::Write(const void *data, UInt32 size, UInt32 *count)) {
    if (count) *count = 0;
    RINOK(ctx->poll())
    if ((ctx->result = ctx->cb->write(ctx->user, data, size))) return E_ABORT;
    if (count) *count = size;
    return S_OK;
}
class Extract final: public IArchiveExtractCallback, public ICryptoGetTextPassword, public CMyUnknownImp {
    Z7_IFACES_IMP_UNK_2(IArchiveExtractCallback, ICryptoGetTextPassword)
    Z7_IFACE_COM7_IMP(IProgress)
public:
    Context *ctx;
    const std::vector<Entry> *entries;
    bool extracting = false;
};
Z7_COM7F_IMF(Extract::SetTotal(UInt64)) { return ctx->poll(); }
Z7_COM7F_IMF(Extract::SetCompleted(const UInt64 *)) { return ctx->poll(); }
Z7_COM7F_IMF(Extract::CryptoGetTextPassword(BSTR *out)) { return ctx->pass(out); }
Z7_COM7F_IMF(Extract::PrepareOperation(Int32)) { return ctx->poll(); }
Z7_COM7F_IMF(Extract::GetStream(UInt32 index, ISequentialOutStream **out, Int32 mode)) {
    *out = nullptr;
    RINOK(ctx->poll())
    extracting = mode == NAskMode::kExtract;
    if (!extracting) return S_OK; // Solid blocks may decode skipped neighbours.
    if (index >= entries->size()) return E_FAIL;
    const auto &e = (*entries)[index];
    if ((ctx->result = ctx->cb->begin(ctx->user, e.path.c_str(), e.size, e.directory))) return E_ABORT;
    if (!e.directory) {
        auto *stream = new(std::nothrow) Output;
        if (!stream) { ctx->result = -1; return E_OUTOFMEMORY; }
        stream->ctx = ctx;
        static_cast<ISequentialOutStream *>(stream)->AddRef();
        *out = stream;
    }
    return S_OK;
}
Z7_COM7F_IMF(Extract::SetOperationResult(Int32 result)) {
    if (result != NOperationResult::kOK) {
        ctx->result = result == NOperationResult::kWrongPassword ||
            (ctx->passwordAsked && (result == NOperationResult::kCRCError || result == NOperationResult::kDataError)) ? -2 : -1;
        return E_ABORT;
    }
    if (extracting && (ctx->result = ctx->cb->end(ctx->user))) return E_ABORT;
    return ctx->poll();
}
static bool flag(IInArchive *arc, UInt32 index, PROPID key) {
    CPropVariant p;
    if (arc->GetProperty(index, key, &p) != S_OK) throw 1;
    return (p.vt == VT_BOOL && p.boolVal) || (p.vt == VT_BSTR && p.bstrVal && *p.bstrVal);
}
static int decode(int fd, const char *kind, const char *password, const SevenCallbacks *cb,
                  void *user, char *error, size_t cap, bool inspect) {
    Context ctx{cb, user, UString(), false, 0};
    if (!ConvertUTF8ToUnicode(AString(password ? password : ""), ctx.password)) return -1;
    // The selected handler must match the catalogue; never auto-detect executables or volumes.
    unsigned id = !strcmp(kind, "zip") ? 1 : !strcmp(kind, "7z") ? 7 : !strcmp(kind, "rar") ? 3 : 0;
    if (!id) return -1;
    unsigned char signature[8] = {};
    if (pread(fd, signature, sizeof signature, 0) < 0) return -1;
    if (id == 3 && !memcmp(signature, "Rar!\x1a\x07\x01\x00", 8)) id = 0xcc;
    GUID cls{0x23170F69, 0x40C1, 0x278A, {0x10,0,0,1,0x10,(Byte)id,0,0}};
    CMyComPtr<IInArchive> arc;
    if (CreateArchiver(&cls, &IID_IInArchive, (void **)&arc) != S_OK) return -1;
    auto *input = new Input; input->fd = fd; input->ctx = &ctx;
    CMyComPtr<IInStream> in = input;
    auto *opener = new Open; opener->ctx = &ctx;
    CMyComPtr<IArchiveOpenCallback> open = opener;
    lseek(fd, 0, SEEK_SET);
    const UInt64 searchLimit = 0;
    HRESULT hr = arc->Open(in, &searchLimit, open);
    if (hr != S_OK) {
        if (ctx.result == 1) return 1;
        if (ctx.passwordAsked) return -2;
        snprintf(error, cap, "This download is not a readable %s archive.", kind);
        return -1;
    }
    UInt32 count = 0;
    if (arc->GetNumberOfItems(&count) != S_OK || count > 100000) return -1;
    // Reject multi-volume archives even when the first file happens to decode.
    CPropVariant volume;
    if (arc->GetArchiveProperty(kpidIsVolume, &volume) != S_OK ||
        (volume.vt == VT_BOOL && volume.boolVal)) {
        snprintf(error, cap, "Multi-part archives aren't supported."); return -1;
    }
    std::vector<Entry> entries;
    std::vector<UInt32> included;
    entries.reserve(count);
    for (UInt32 i = 0; i < count; ++i) {
        if (ctx.poll() != S_OK) return ctx.result;
        CPropVariant p, size, mode, attr;
        if (arc->GetProperty(i, kpidPath, &p) != S_OK || p.vt != VT_BSTR || !p.bstrVal ||
            arc->GetProperty(i, kpidSize, &size) != S_OK ||
            arc->GetProperty(i, kpidPosixAttrib, &mode) != S_OK ||
            arc->GetProperty(i, kpidAttrib, &attr) != S_OK) return -1;
        AString path;
        UString wide(p.bstrVal);
        if (wide.Len() != SysStringLen(p.bstrVal)) return -1; // Embedded NUL.
        ConvertUnicodeToUTF8(wide, path);
        bool directory = flag(arc, i, kpidIsDir);
        int64_t bytes = size.vt == VT_UI8 && size.uhVal.QuadPart <= INT64_MAX ? (int64_t)size.uhVal.QuadPart : -1;
        if (directory) bytes = 0;
        if (bytes < 0) return -1;
        unsigned unixMode = mode.vt == VT_UI4 ? mode.ulVal :
            attr.vt == VT_UI4 && (attr.ulVal & 0x8000) ? attr.ulVal >> 16 : 0;
        unsigned type = unixMode & S_IFMT;
        bool unsafe = (type && type != S_IFREG && type != S_IFDIR) ||
            flag(arc, i, kpidSymLink) || flag(arc, i, kpidHardLink) ||
            flag(arc, i, kpidIsAltStream) || flag(arc, i, kpidIsAnti);
        entries.push_back({std::string(path.Ptr(), path.Len()), bytes, directory});
        int check = cb->check(user, path.Ptr(), bytes, directory, unsafe);
        if (!check) included.push_back(i);
        else if (check != 2) return check;
    }
    if (inspect) { arc->Close(); return 0; }
    auto *extractor = new Extract; extractor->ctx = &ctx; extractor->entries = &entries;
    CMyComPtr<IArchiveExtractCallback> extract = extractor;
    hr = included.empty() ? S_OK : arc->Extract(included.data(), (UInt32)included.size(), 0, extract);
    arc->Close();
    return ctx.result ? ctx.result : hr == S_OK ? 0 : -1;
}
static int invoke(int fd, const char *kind, const char *password,
                  const SevenCallbacks *cb, void *user, char *error, size_t cap, bool inspect) {
    int result;
    try { result = decode(fd, kind, password, cb, user, error, cap, inspect); }
    catch (const std::bad_alloc &) {
        snprintf(error, cap, "Not enough memory to extract this archive on the PS5. Use Orbit Zero to extract it on a computer.");
        return -1;
    } catch (...) { result = -1; }
    if (result == -2) snprintf(error, cap, "The archive password is missing or incorrect. The downloaded archive was kept.");
    else if (result == -1 && !*error) snprintf(error, cap, "The archive is damaged or uses an unsupported compression method.");
    return result;
}

extern "C" int seven_inspect(int fd, const char *kind, const char *password,
                            const SevenCallbacks *cb, void *user, char *error, size_t cap) {
    return invoke(fd, kind, password, cb, user, error, cap, true);
}
extern "C" int seven_extract(int fd, const char *kind, const char *password,
                            const SevenCallbacks *cb, void *user, char *error, size_t cap) {
    return invoke(fd, kind, password, cb, user, error, cap, false);
}
