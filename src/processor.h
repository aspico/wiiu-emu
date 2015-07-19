#pragma once
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <Windows.h>
#include "modules/coreinit/coreinit_mutex.h"

struct OSThread;
struct ThreadState;

struct Fiber
{
   Fiber()
   {
      handle = CreateFiber(0, &Fiber::fiberEntryPoint, this);
   }

   ~Fiber()
   {
      DeleteFiber(handle);
   }

   static void __stdcall fiberEntryPoint(void *param);

   uint32_t coreID;
   void *handle = nullptr;
   void *parentFiber = nullptr;
   OSThread *thread = nullptr;
   ThreadState state;
};

struct Core
{
   Core(uint32_t id) :
      id(id)
   {
   }

   uint32_t id;
   Fiber *currentFiber = nullptr;
   void *primaryFiber = nullptr;
   std::thread thread;
};

class Processor
{
public:
   Processor(size_t cores);

   // Processor
   void start();
   void join();

   // Fiber
   void queue(Fiber *fiber);
   void reschedule(bool hasSchedulerLock, bool yield = false);
   void yield();
   void exit();

   Fiber *createFiber();
   Fiber *getCurrentFiber();
   Fiber *peekNextFiber(uint32_t core);

   template<typename LockType>
   void wait(LockType &lock)
   {
      lock.unlock();
      reschedule(false);
   }

   // Core
   uint32_t getCoreID();
   uint32_t getCoreCount();

protected:
   friend Core;
   friend Fiber;

   void coreEntryPoint(Core *core);
   void fiberEntryPoint(Fiber *fiber);

private:
   std::atomic<bool> mRunning;
   std::vector<Core*> mCores;
   std::mutex mMutex;
   std::condition_variable mCondition;
   std::vector<Fiber *> mFiberQueue;
   std::vector<Fiber *> mFiberList;
};

extern Processor
gProcessor;
