#include "hermes.h"
#include "impl/common.h"

#include <cstring>

#include <chrono>

using namespace std;

namespace hermes::impl {
    static unordered_map<uint64_t, uint64_t> pending_size;

    int getattr(const char *path, struct stat *stbuf) {
        auto ctx = static_cast<hermes::impl::context *>(fuse_get_context()->private_data);

        // Is root
        if (strcmp(path, "/") == 0) {
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 1;
            return 0;
        }

        auto resp = ctx->backend->fetch_metadata(path);

        if (resp) {
            *stbuf = resp->to_stat();
            return 0;
        } else {
            return -ENOENT;
        }
    }

    int chmod(const char *path, mode_t mode) {
        auto ctx = static_cast<hermes::impl::context *>(fuse_get_context()->private_data);
        auto resp = ctx->backend->fetch_metadata(path);

        if (!resp) {
            return -ENOENT;
        }

        resp->mode = (resp->mode & S_IFMT) + (mode & ~S_IFMT);

        ctx->backend->put_metadata(path, *resp);

        return 0;
    }

    int chown(const char *path, uid_t uid, gid_t gid) {
        auto ctx = static_cast<hermes::impl::context *>(fuse_get_context()->private_data);
        auto resp = ctx->backend->fetch_metadata(path);

        if (!resp) {
            return -ENOENT;
        }

        resp->uid = uid;
        resp->gid = gid;

        ctx->backend->put_metadata(path, *resp);

        return 0;
    }

    int truncate(const char *path, off_t size) {
        auto ctx = static_cast<hermes::impl::context *>(fuse_get_context()->private_data);
        auto resp = ctx->backend->fetch_metadata(path);

        if (!resp) {
            return -ENOENT;
        }

        resp->size = size;

        ctx->backend->put_metadata(path, *resp);

        return 0;
    }

    int open(const char *path, struct fuse_file_info *fi) {
        auto ctx = static_cast<hermes::impl::context *>(fuse_get_context()->private_data);

        auto resp = ctx->backend->fetch_metadata(path);

        // TODO: do we need to follow symbolic links here?
        if (!resp) {
            return -ENOENT;
        } else if (!resp->is_file()) {
            return -EISDIR;
        } else {
            fi->fh = resp->id;
            // cout<<">> FileID: "<<resp->id<<endl;
            return 0;
        }
    }

    int read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
        auto ctx = static_cast<hermes::impl::context *>(fuse_get_context()->private_data);

        // cout<<">> Reading: "<<fi->fh<<endl;

        /*
        auto mtresp = ctx->backend->fetch_metadata(path);

        // TODO: upate atim
        // TODO: handle dir & symlink

        if(!mtresp) {
          return -ENOENT;
        } else if(mtresp->size == 0) {
          return 0;
        } else {
          ctx->backend->fetch_content(mtresp->id, offset, size, buf);
          return size;
        }
        */

        ctx->backend->fetch_content(fi->fh, offset, size, buf);
        return size;
    }

    int write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
        auto ctx = static_cast<hermes::impl::context *>(fuse_get_context()->private_data);

        // TODO: Alter file size and mtime in release

        // auto mtresp = ctx->backend->fetch_metadata(path);

        // TODO: lock
        ctx->backend->put_content(fi->fh, offset, string_view(buf, size));

        /*
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);

        mtresp->mtim = now;
        mtresp->atim = now;
        if(offset + size > mtresp->size) mtresp->size = offset + size;
        ctx->backend->put_metadata(path, *mtresp);
        */

        uint64_t new_size = offset + size;
        uint64_t &saved_size = pending_size[fi->fh]; // uint64_t will be default-initialized into 0
        if (saved_size < new_size)
            saved_size = new_size;

        return size;
    }

    int release(const char *path, struct fuse_file_info *fi) {
        auto saved_size = pending_size.find(fi->fh);
        if (saved_size == pending_size.end()) return 0;

        auto ctx = static_cast<hermes::impl::context *>(fuse_get_context()->private_data);

        auto mtresp = ctx->backend->fetch_metadata(path);

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);

        // Written
        mtresp->mtim = now;
        if (saved_size->second > mtresp->size) mtresp->size = saved_size->second;
        pending_size.erase(saved_size);

        ctx->backend->put_metadata(path, *mtresp);
        // cout<<"Write"<<endl;
        return 0;
    }

    int create(const char *path, mode_t mode, struct fuse_file_info *fi) {
        // auto before = std::chrono::high_resolution_clock::now();

        auto fctx = fuse_get_context();
        auto ctx = static_cast<hermes::impl::context *>(fctx->private_data);
        if (ctx->backend->fetch_metadata(path))
            return -EEXIST;

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);

        hermes::metadata mt = {
            .id = ctx->backend->next_id(),
            .mode = mode,
            .uid = fctx->uid,
            .gid = fctx->gid,
            .size = 0,
            .atim = now,
            .mtim = now,
            .ctim = now,
        };

        // TODO: lock
        // TODO: check permission and deal with errors
        ctx->backend->put_metadata(path, mt);

        // auto after = std::chrono::high_resolution_clock::now();
        // auto diff = after - before;

        // cout<<"Time used: "<<std::chrono::duration_cast<std::chrono::nanoseconds>(diff).count()<<endl;
        return 0;
    }

    int utimens(const char *path, const struct timespec tv[2]) {
        auto ctx = static_cast<hermes::impl::context *>(fuse_get_context()->private_data);
        auto resp = ctx->backend->fetch_metadata(path);

        if (!resp) {
            return -ENOENT;
        }

        resp->atim = tv[0];
        resp->mtim = tv[1];

        ctx->backend->put_metadata(path, *resp);

        return 0;
    }

    int unlink(const char *path) {
        auto ctx = static_cast<hermes::impl::context *>(fuse_get_context()->private_data);
        auto resp = ctx->backend->remove_metadata(path);
        if (resp) {
            return 0;
        }
        return -ENOENT;
    }

    int rename(const char *from, const char *to) {
        auto ctx = static_cast<hermes::impl::context *>(fuse_get_context()->private_data);

        /*
        if(flags == RENAME_EXCHANGE) {
          auto oriFrom = ctx->backend->remove_metadata(from);
          if(!oriFrom) return -ENOENT;

          auto oriTo = ctx->backend->remove_metadata(to);
          if(!oriTo) {
            ctx->backend->put_metadata(from, *oriFrom);
            return -ENOENT;
          }

          struct timespec now;
          clock_gettime(CLOCK_REALTIME, &now);

          oriFrom->mtim = now;
          oriTo->mtim = now;

          ctx->backend->put_metadata(to, *oriFrom);
          ctx->backend->put_metadata(from, *oriTo);

          const auto contFrom = ctx->backend->remove_content(from);
          const auto contTo = ctx->backend->remove_content(to);

          if(contFrom) ctx->backend->put_content(to, *contFrom);
          if(contTo) ctx->backend->put_content(from, *contTo);

          return 0;
        } else if(flags == RENAME_NOREPLACE) {
          if(ctx->backend->fetch_metadata(to))
            return -EEXIST;
        }
        */

        auto original = ctx->backend->remove_metadata(from);
        if (!original) return -ENOENT;

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);

        original->mtim = now;
        ctx->backend->put_metadata(to, *original);

        return 0;
    }
}
