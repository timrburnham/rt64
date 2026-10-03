//
// RT64
//

#include "rt64_raster_shader_cache.h"

#include "common/rt64_thread.h"

#define ENABLE_OPTIMIZED_SHADER_GENERATION

namespace RT64 {
    // RasterShaderCache::CompilationThread
    
    RasterShaderCache::CompilationThread::CompilationThread(RasterShaderCache *shaderCache) {
        assert(shaderCache != nullptr);

        this->shaderCache = shaderCache;

        // Publish the running state before launching the worker, which can
        // begin waiting on the queue as soon as the thread is created.
        threadRunning.store(true, std::memory_order_release);
        thread = std::make_unique<std::thread>(&CompilationThread::loop, this);
    }

    RasterShaderCache::CompilationThread::~CompilationThread() {
        {
            std::unique_lock<std::mutex> queueLock(shaderCache->descQueueMutex);
            threadRunning.store(false, std::memory_order_release);
        }
        shaderCache->descQueueChanged.notify_all();
        thread->join();
        thread.reset(nullptr);
    }

    void RasterShaderCache::CompilationThread::loop() {
        Thread::setCurrentThreadName("RT64 Shader");

        // The shader compilation thread should have idle priority by default as the application can use the ubershader in the meantime.
        Thread::setCurrentThreadPriority(Thread::Priority::Idle);

        while (true) {
            ShaderDescription shaderDesc;
            
            // Wait until work is queued or shutdown is requested, then remove
            // one item and account for it while holding the queue lock.
            {
                std::unique_lock<std::mutex> queueLock(shaderCache->descQueueMutex);
                shaderCache->descQueueChanged.wait(queueLock, [this]() {
                    return !threadRunning.load(std::memory_order_acquire) || !shaderCache->descQueue.empty();
                });

                if (!threadRunning.load(std::memory_order_acquire)) {
                    return;
                }

                assert(!shaderCache->descQueue.empty());
                shaderDesc = shaderCache->descQueue.front();
                shaderCache->descQueue.pop();
                shaderCache->activeCompileCount++;
            }
            
            // Compile the shader and publish it before marking this job idle.
            assert((shaderCache->shaderUber != nullptr) && "Ubershader should've been created by the time a new shader is submitted to the cache.");
            const RenderPipelineLayout *uberPipelineLayout = shaderCache->shaderUber->pipelineLayout.get();
            const RenderMultisampling multisampling = shaderCache->multisampling;
            std::unique_ptr<RasterShader> newShader = std::make_unique<RasterShader>(shaderCache->device, shaderDesc, uberPipelineLayout, shaderCache->shaderFormat, multisampling, shaderCache->shaderCompiler.get(), &shaderCache->optimizerCacheSPIRV);

            {
                const std::unique_lock<std::mutex> lock(shaderCache->GPUShadersMutex);
                shaderCache->GPUShaders[shaderDesc.hash()] = std::move(newShader);
            }

            {
                std::unique_lock<std::mutex> queueLock(shaderCache->descQueueMutex);
                assert(shaderCache->activeCompileCount > 0);
                shaderCache->activeCompileCount--;
            }
            shaderCache->descQueueChanged.notify_all();
        }
    }

    // RasterShaderCache

    RasterShaderCache::RasterShaderCache(uint32_t threadCount, uint32_t ubershaderThreadCount) {
        assert(threadCount > 0);

        this->threadCount = threadCount;
        this->ubershaderThreadCount = ubershaderThreadCount;

#ifdef ENABLE_OPTIMIZED_SHADER_GENERATION
#   ifdef _WIN32
        shaderCompiler = std::make_unique<ShaderCompiler>();
#   endif

        for (uint32_t t = 0; t < threadCount; t++) {
            compilationThreads.push_back(std::make_unique<CompilationThread>(this));
        }
#endif
    }

    RasterShaderCache::~RasterShaderCache() {
        compilationThreads.clear();
    }

    void RasterShaderCache::setup(RenderDevice *device, RenderShaderFormat shaderFormat, const ShaderLibrary *shaderLibrary, const RenderMultisampling &multisampling) {
        assert(device != nullptr);

        this->device = device;
        this->shaderFormat = shaderFormat;
        this->multisampling = multisampling;

        shaderUber = std::make_unique<RasterShaderUber>(device, shaderFormat, multisampling, shaderLibrary, ubershaderThreadCount);
        usesHDR = shaderLibrary->usesHDR;

        // Initialize the re-spirv optimizer cache.
        if (shaderFormat == RenderShaderFormat::SPIRV) {
            optimizerCacheSPIRV.initialize();
        }
    }

    void RasterShaderCache::submit(const ShaderDescription &desc) {
        // Keep the submission serialized until the descriptor is queued, so
        // waitForAll() cannot pass an item between these two operations.
        std::unique_lock<std::mutex> submissionLock(submissionMutex);

        // Verify if an entry with the same hash was already submitted before.
        const uint64_t shaderHash = desc.hash();
        bool &found = shaderHashes[shaderHash];
        if (found) {
            return;
        }
        found = true;

        // Push a new shader compilation to the queue.
        {
            const std::unique_lock<std::mutex> queueLock(descQueueMutex);
            descQueue.push(desc);
        }

        descQueueChanged.notify_all();
    }
    
    void RasterShaderCache::waitForAll() {
        // Serialize with submit() so an item cannot be inserted between the
        // queue clear and the idle predicate becoming true.
        std::unique_lock<std::mutex> submissionLock(submissionMutex);
        std::unique_lock<std::mutex> queueLock(descQueueMutex);
        // Cache destruction invalidates queued-but-not-started work, matching
        // the previous behavior. Jobs already removed by workers must finish.
        descQueue = std::queue<ShaderDescription>();
        descQueueChanged.wait(queueLock, [this]() { return activeCompileCount == 0; });
    }

    void RasterShaderCache::destroyAll() {
        {
            std::unique_lock<std::mutex> lock(GPUShadersMutex);
            GPUShaders.clear();
        }

        {
            std::unique_lock<std::mutex> queueLock(submissionMutex);
            shaderHashes.clear();
        }
    }

    RasterShader *RasterShaderCache::getGPUShader(const ShaderDescription &desc) {
        const uint64_t shaderHash = desc.hash();

        const std::unique_lock<std::mutex> lock(GPUShadersMutex);
        auto shaderIt = GPUShaders.find(shaderHash);
        if (shaderIt == GPUShaders.end()) {
            return nullptr;
        }

        return shaderIt->second.get();
    }

    RasterShaderUber *RasterShaderCache::getGPUShaderUber() const {
        return shaderUber.get();
    }

    uint32_t RasterShaderCache::shaderCount() {
        std::unique_lock<std::mutex> lock(GPUShadersMutex);
        return GPUShaders.size();
    }
};
