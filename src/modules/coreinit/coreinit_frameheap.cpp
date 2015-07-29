#include "coreinit.h"
#include "coreinit_memheap.h"
#include "coreinit_frameheap.h"
#include "system.h"

#pragma pack(push, 1)

struct FrameHeapState
{
   uint32_t tag;
   uint32_t top;
   uint32_t bottom;
   p32<FrameHeapState> previous;
};

struct FrameHeap : CommonHeap
{
   uint32_t top;
   uint32_t bottom;
   uint32_t size;
   p32<FrameHeapState> state;
};

#pragma pack(pop)

FrameHeap *
MEMCreateFrmHeap(FrameHeap *heap, uint32_t size)
{
   return MEMCreateFrmHeapEx(heap, size, 0);
}

FrameHeap *
MEMCreateFrmHeapEx(FrameHeap *heap, uint32_t size, uint16_t flags)
{
   // Allocate memory
   auto base = gMemory.untranslate(heap);
   gMemory.alloc(base, size);

   // Setup state
   heap->size = size;
   heap->top = base + size;
   heap->bottom = base + sizeof(FrameHeap) + sizeof(FrameHeapState);
   heap->state = make_p32<FrameHeap>(base + sizeof(FrameHeap));
   heap->state->tag = 0;
   heap->state->top = heap->top;
   heap->state->bottom = heap->bottom;
   heap->state->previous = nullptr;

   // Setup common header
   MEMiInitHeapHead(heap, HeapType::FrameHeap, heap->bottom, heap->top);
   return heap;
}

void *
MEMDestroyFrmHeap(FrameHeap *heap)
{
   MEMiFinaliseHeap(heap);
   gMemory.free(gMemory.untranslate(heap));
   return heap;
}

void *
MEMAllocFromFrmHeap(FrameHeap *heap, uint32_t size)
{
   ScopedSpinLock lock(&heap->lock);
   return MEMAllocFromFrmHeapEx(heap, size, 4);
}

void *
MEMAllocFromFrmHeapEx(FrameHeap *heap, uint32_t size, int alignment)
{
   ScopedSpinLock lock(&heap->lock);
   auto direction = HeapDirection::FromBottom;
   auto offset = 0u;

   if (alignment < 0) {
      alignment = -alignment;
      direction = HeapDirection::FromTop;
   }

   // Align size
   size = alignUp(size, alignment);

   // Ensure there is sufficient space on the heap
   if (heap->state->top - heap->state->bottom < size) {
      return nullptr;
   }

   // Allocate
   if (direction == HeapDirection::FromBottom) {
      offset = heap->state->bottom;
      heap->state->bottom += size;
   } else if (direction == HeapDirection::FromTop) {
      heap->state->top -= size;
      offset = heap->state->top;
   }

   // Align offset
   offset = alignUp(offset, alignment);
   return make_p32<void>(offset);
}

void
MEMFreeToFrmHeap(FrameHeap *heap, FrameHeapFreeMode::Flags mode)
{
   ScopedSpinLock lock(&heap->lock);

   if (mode & FrameHeapFreeMode::Top) {
      if (heap->state->previous) {
         heap->state->top = heap->state->previous->top;
      } else {
         heap->state->top = heap->top;
      }
   }

   if (mode & FrameHeapFreeMode::Bottom) {
      if (heap->state->previous) {
         heap->state->bottom = heap->state->previous->bottom;
      } else {
         heap->state->bottom = heap->bottom;
      }
   }
}

BOOL
MEMRecordStateForFrmHeap(FrameHeap *heap, uint32_t tag)
{
   ScopedSpinLock lock(&heap->lock);
   FrameHeapState *state = reinterpret_cast<FrameHeapState*>(MEMAllocFromFrmHeapEx(heap, sizeof(FrameHeapState), 4));
   
   if (!state) {
      return FALSE;
   }

   state->previous = heap->state;
   state->tag = tag;
   state->top = heap->state->top;
   state->bottom = heap->state->bottom;
   heap->state = state;
   return TRUE;
}

BOOL
MEMFreeByStateToFrmHeap(FrameHeap *heap, uint32_t tag)
{
   ScopedSpinLock lock(&heap->lock);

   if (tag == 0) {
      if (!heap->state->previous) {
         return false;
      }

      heap->state = heap->state->previous;
      return true;
   }

   auto state = heap->state;

   while (state) {
      if (state->tag == tag) {
         heap->state = state;
         return true;
      }

      state = state->previous;
   }

   return false;
}

uint32_t
MEMAdjustFrmHeap(FrameHeap *heap)
{
   ScopedSpinLock lock(&heap->lock);

   if (heap->state->top != heap->top) {
      return heap->size;
   }

   heap->top = heap->bottom;
   heap->state->top = heap->top;
   heap->size = heap->state->top - gMemory.untranslate(heap);
   return heap->size;
}

uint32_t
MEMResizeForMBlockFrmHeap(FrameHeap *heap, uint32_t addr, uint32_t size)
{
   ScopedSpinLock lock(&heap->lock);

   if (addr > heap->state->bottom || addr < heap->state->bottom) {
      gLog->error("Invalid block address in MEMResizeForMBlockFrmHeap");
      return 0;
   }

   auto curSize = heap->state->bottom - addr;

   if (size < curSize) {
      heap->state->bottom = addr + size;
      return 0;
   }

   auto difSize = size - curSize;

   if (heap->state->top - heap->state->bottom < difSize) {
      // Not enough free space
      return 0;
   }

   heap->state->bottom += difSize;
   return size;
}

uint32_t
MEMGetAllocatableSizeForFrmHeap(FrameHeap *heap)
{
   ScopedSpinLock lock(&heap->lock);
   return MEMGetAllocatableSizeForFrmHeapEx(heap, 4);
}

uint32_t
MEMGetAllocatableSizeForFrmHeapEx(FrameHeap *heap, int alignment)
{
   ScopedSpinLock lock(&heap->lock);
   auto bottom = alignUp(heap->state->bottom, alignment);
   auto top = alignDown(heap->state->top, alignment);
   return top - bottom;
}

void
CoreInit::registerFrameHeapFunctions()
{
   RegisterKernelFunction(MEMCreateFrmHeap);
   RegisterKernelFunction(MEMCreateFrmHeapEx);
   RegisterKernelFunction(MEMDestroyFrmHeap);
   RegisterKernelFunction(MEMAllocFromFrmHeap);
   RegisterKernelFunction(MEMAllocFromFrmHeapEx);
   RegisterKernelFunction(MEMFreeToFrmHeap);
   RegisterKernelFunction(MEMRecordStateForFrmHeap);
   RegisterKernelFunction(MEMFreeByStateToFrmHeap);
   RegisterKernelFunction(MEMAdjustFrmHeap);
   RegisterKernelFunction(MEMResizeForMBlockFrmHeap);
   RegisterKernelFunction(MEMGetAllocatableSizeForFrmHeap);
   RegisterKernelFunction(MEMGetAllocatableSizeForFrmHeapEx);
}
