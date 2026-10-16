#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <watx/platform/threads.h>
#include <watx/runtime/executor.h>
#include <watx/runtime/sync.h>
namespace watx {
namespace {
class Lock {
    RuntimeMutex &mutex_;

  public:
    explicit Lock(RuntimeMutex &mutex) : mutex_(mutex) {
        mutex_.lock();
    }
    ~Lock() {
        mutex_.unlock();
    }

  private:
    Lock(const Lock &);
    Lock &operator=(const Lock &);
};
}
void InterpreterExecutor::execute(RuntimeFrame &frame) {
    runtime_interpret(&frame);
}
struct ThreadedExecutor::Impl {
    struct Job {
        RuntimeFrame *frame;
        bool done, failed;
        std::string error;
        explicit Job(RuntimeFrame *value) : frame(value), done(false), failed(false) {}
    };
    struct Worker {
        Impl *pool;
        uint32_t index;
        RuntimeThread thread;
        Worker(Impl *p, uint32_t i) : pool(p), index(i) {}
    };
    unsigned count;
    InterpreterExecutor interpreter;
    RuntimeExecutor *backend;
    RuntimeMutex mutex;
    RuntimeCondition changed;
    std::deque<Job *> pending;
    std::vector<Worker *> workers;
    bool stopping, startup_failed;
    unsigned ready;
    Impl(unsigned requested, RuntimeExecutor *executor)
        : count(worker_count(requested)), backend(executor ? executor : &interpreter),
          stopping(false), startup_failed(false), ready(0) {}
    ~Impl() {
        stop();
    }
    void stop() {
        {
            Lock lock(mutex);
            stopping = true;
            changed.notify_all();
        }
        for (std::size_t i = 0; i < workers.size(); ++i) {
            workers[i]->thread.join();
            delete workers[i];
        }
        workers.clear();
    }
    static void entry(void *context) {
        Worker *worker = static_cast<Worker *>(context);
        Impl *pool = worker->pool;
        bool context_set = runtime_set_thread_context(pool, worker->index + 1, 0);
        {
            Lock lock(pool->mutex);
            ++pool->ready;
            if (!context_set)
                pool->startup_failed = true;
            pool->changed.notify_all();
        }
        if (!context_set)
            return;
        for (;;) {
            Job *job = 0;
            {
                Lock lock(pool->mutex);
                while (pool->pending.empty() && !pool->stopping)
                    pool->changed.wait(pool->mutex, -1);
                if (pool->pending.empty() && pool->stopping)
                    break;
                job = pool->pending.front();
                pool->pending.pop_front();
            }
            try {
                pool->backend->execute(*job->frame);
            } catch (const std::exception &error) {
                job->failed = true;
                try {
                    job->error = error.what();
                } catch (...) {
                }
            } catch (...) {
                job->failed = true;
                try {
                    job->error = "execution worker failed";
                } catch (...) {
                }
            }
            {
                Lock lock(pool->mutex);
                job->done = true;
                pool->changed.notify_all();
            }
        }
        runtime_clear_thread_context();
    }
    void start() {
        if (count == 1)
            return;
        workers.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            std::auto_ptr<Worker> worker(new Worker(this, i));
            workers.push_back(worker.get());
            worker.release();
            if (!workers.back()->thread.start(entry, workers.back()))
                throw std::runtime_error("could not create execution worker");
        }
        Lock lock(mutex);
        while (ready < count)
            changed.wait(mutex, -1);
        if (startup_failed)
            throw std::runtime_error("could not initialize execution worker context");
    }
};
ThreadedExecutor::ThreadedExecutor(unsigned threads, RuntimeExecutor *backend)
    : impl_(new Impl(threads, backend)) {
    try {
        impl_->start();
    } catch (...) {
        delete impl_;
        impl_ = 0;
        throw;
    }
}
ThreadedExecutor::~ThreadedExecutor() {
    delete impl_;
}
void ThreadedExecutor::execute(RuntimeFrame &frame) {
    if (impl_->count == 1 || runtime_current_thread_group() == impl_) {
        impl_->backend->execute(frame);
        return;
    }
    Impl::Job job(&frame);
    {
        Lock lock(impl_->mutex);
        if (impl_->stopping)
            throw std::runtime_error("execution worker pool is stopping");
        impl_->pending.push_back(&job);
        impl_->changed.notify_all();
        while (!job.done)
            impl_->changed.wait(impl_->mutex, -1);
    }
    if (job.failed)
        throw std::runtime_error(job.error.empty() ? "execution worker failed" : job.error);
}
unsigned ThreadedExecutor::thread_count() const {
    return impl_->count;
}
}
